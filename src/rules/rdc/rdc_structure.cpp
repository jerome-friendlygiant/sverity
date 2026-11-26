// Reset-domain structure checks on the shared model: crossings between reset domains, reset trees built from logic,
// reset synchroniser stage fanout, partially reset state, multiple asynchronous controls, exceptions, polarity and the
// explicit "not modelled" markers (sequencing, power intent, physical recovery/removal).
#include "rules/rdc/rdc_shared.h"
#include "rules/sync_attribute.h"
#include <bit>
#include <cctype>
#include <optional>
#include <deque>
#include <string_view>
#include <unordered_map>
#include <functional>

#include "domains/batch_reach.h"
#include "sverity/waiver.h"   // globMatch
#include "slang/ast/ASTVisitor.h"
#include "slang/text/SourceManager.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/expressions/SelectExpressions.h"

namespace sverity::rdc {

using namespace model;

namespace {
// An X or Z bit in a literal of the driver expression: the value the net takes is then not a function of its inputs alone.
bool containsUnknownLiteral(const slang::ast::Expression& expr) {
    struct Finder : slang::ast::ASTVisitor<Finder, slang::ast::VisitFlags::AllGood> {
        bool found = false;
        void handle(const slang::ast::IntegerLiteral& literal) { found = found || literal.getValue().hasUnknown(); }
    } finder;
    expr.visit(finder);
    return finder.found;
}

// implicit conversions (a literal widened to the operator's type) do not change a value
const slang::ast::Expression& stripConversions(const slang::ast::Expression& e) {
    const slang::ast::Expression* x = &e;
    while (x->kind == slang::ast::ExpressionKind::Conversion) x = &x->as<slang::ast::ConversionExpression>().operand();
    return *x;
}

// The X or Z literals of `expr` that can reach bit `bit` (LSB 0) of its value: a concatenation is split by operand (the first
// operand is the most significant), anything else is taken whole. Without a bit, any literal counts.
bool unknownLiteralReaches(const slang::ast::Expression& expr, std::optional<uint64_t> bit) {
    const slang::ast::Expression& e = stripConversions(expr);
    if (!bit) return containsUnknownLiteral(expr);
    using E = slang::ast::ExpressionKind;
    // a literal reaches bit `bit` only through its own bit; a bit past its width is zero-extended
    if (e.kind == E::IntegerLiteral) {
        const auto& value = e.as<slang::ast::IntegerLiteral>().getValue();
        if (!value.hasUnknown() || *bit >= value.getBitWidth()) return false;
        const slang::logic_t b = value[static_cast<int32_t>(*bit)];
        return b.value != 0 && b.value != 1;
    }
    // bitwise operators keep bits apart: bit i of the result depends on bit i of each operand only
    if (e.kind == E::UnaryOp && e.as<slang::ast::UnaryExpression>().op == slang::ast::UnaryOperator::BitwiseNot)
        return unknownLiteralReaches(e.as<slang::ast::UnaryExpression>().operand(), bit);
    if (e.kind == E::BinaryOp) {
        const auto op = e.as<slang::ast::BinaryExpression>().op;
        if (op == slang::ast::BinaryOperator::BinaryAnd || op == slang::ast::BinaryOperator::BinaryOr ||
            op == slang::ast::BinaryOperator::BinaryXor || op == slang::ast::BinaryOperator::BinaryXnor) {
            const auto& binary = e.as<slang::ast::BinaryExpression>();
            return unknownLiteralReaches(binary.left(), bit) || unknownLiteralReaches(binary.right(), bit);
        }
        return containsUnknownLiteral(expr);
    }
    if (e.kind != E::Concatenation) return containsUnknownLiteral(expr);
    uint64_t hi = e.type ? e.type->getBitstreamWidth() : 0;   // bits [hi-1:0] remain to be assigned to operands
    if (hi == 0 || *bit >= hi) return containsUnknownLiteral(expr);
    for (const auto* operand : e.as<slang::ast::ConcatenationExpression>().operands()) {
        const uint64_t w = operand->type ? operand->type->getBitstreamWidth() : 0;
        if (w == 0 || w > hi) return containsUnknownLiteral(expr);
        if (*bit >= hi - w) return unknownLiteralReaches(*operand, *bit - (hi - w));
        hi -= w;
    }
    return false;
}

// one bit of a net by a constant index (`a[1]`, `a[3:3]`): a selected bit is a single signal for the inversion forms
bool isBitSelect(const slang::ast::Expression& expr) {
    const slang::ast::Expression& e = stripConversions(expr);
    if (e.kind == slang::ast::ExpressionKind::ElementSelect) {
        const auto& select = e.as<slang::ast::ElementSelectExpression>();
        return stripConversions(select.value()).kind == slang::ast::ExpressionKind::NamedValue && select.selector().getConstant() != nullptr;
    }
    return false;
}

// `~x` and `!x` of one named value or one selected bit: an inversion of a single signal. A logical not of a wider operand is a
// reduction: it is an inversion only when no bit of the operand is a constant (`!{rst_n, 1'b1}` is constant 0, `!a` with
// `a = {rst_n, 1'b1}` is too).
using DriverOf = std::function<const slang::ast::Expression*(const slang::ast::Symbol*)>;
bool holdsConstantBit(const slang::ast::Expression& expr, const DriverOf& driverOf, int depth = 0) {
    const slang::ast::Expression& e = stripConversions(expr);
    if (e.kind == slang::ast::ExpressionKind::IntegerLiteral) return true;
    if (depth >= 16) return false;
    if (e.kind == slang::ast::ExpressionKind::Concatenation) {
        for (const auto* operand : e.as<slang::ast::ConcatenationExpression>().operands())
            if (holdsConstantBit(*operand, driverOf, depth + 1)) return true;
        return false;
    }
    if (e.kind == slang::ast::ExpressionKind::NamedValue && driverOf) {
        const slang::ast::Expression* driver = driverOf(e.getSymbolReference());
        return driver != nullptr && holdsConstantBit(*driver, driverOf, depth + 1);
    }
    return false;
}

bool invertingUnaryForm(const slang::ast::Expression& expr, const DriverOf& driverOf) {
    if (expr.kind != slang::ast::ExpressionKind::UnaryOp) return false;
    const auto& unary = expr.as<slang::ast::UnaryExpression>();
    if (unary.op != slang::ast::UnaryOperator::BitwiseNot && unary.op != slang::ast::UnaryOperator::LogicalNot) return false;
    if (unary.op == slang::ast::UnaryOperator::LogicalNot && unary.operand().type && unary.operand().type->getBitstreamWidth() != 1 &&
        holdsConstantBit(unary.operand(), driverOf))
        return false;
    return stripConversions(unary.operand()).kind == slang::ast::ExpressionKind::NamedValue || isBitSelect(unary.operand());
}

// `x ^ 1'b1` and `x ~^ 1'b0` (either operand order) are the exact inversion of x. With a leaf, x must be that leaf, read through
// exact copies (`wire a = rst_n; ... a ^ 1'b1`); without one, any non-constant x is inverted.
bool isExactCopyOf(const slang::ast::Expression& operand, const slang::ast::Symbol* leaf, const DriverOf& driverOf, int depth = 0) {
    const slang::ast::Expression& e = stripConversions(operand);
    if (e.kind != slang::ast::ExpressionKind::NamedValue) return false;
    const slang::ast::Symbol* symbol = e.getSymbolReference();
    if (symbol == leaf) return true;
    if (depth >= 16 || !driverOf) return false;
    const slang::ast::Expression* driver = driverOf(symbol);
    return driver != nullptr && isExactCopyOf(*driver, leaf, driverOf, depth + 1);
}

bool invertingConstantForm(const slang::ast::Expression& expr, const slang::ast::Symbol* leaf, const DriverOf& driverOf) {
    if (expr.kind != slang::ast::ExpressionKind::BinaryOp) return false;
    const auto& binary = expr.as<slang::ast::BinaryExpression>();
    const bool xorForm = binary.op == slang::ast::BinaryOperator::BinaryXor;
    const bool xnorForm = binary.op == slang::ast::BinaryOperator::BinaryXnor;
    if (!xorForm && !xnorForm) return false;
    // a compile-time constant: a literal, or a parameter or localparam (`rst_n ^ ACTIVE_LOW`)
    const auto constantOf = [](const slang::ast::Expression& operand) -> std::optional<slang::ConstantValue> {
        const slang::ast::Expression& e = stripConversions(operand);
        if (e.kind == slang::ast::ExpressionKind::IntegerLiteral) return slang::ConstantValue(e.as<slang::ast::IntegerLiteral>().getValue());
        const slang::ast::Symbol* symbol = e.kind == slang::ast::ExpressionKind::NamedValue ? e.getSymbolReference() : nullptr;
        if (symbol && symbol->kind == slang::ast::SymbolKind::Parameter) return symbol->as<slang::ast::ParameterSymbol>().getValue();
        return std::nullopt;
    };
    const auto constantBit = [&](const slang::ast::Expression& operand, uint64_t want) {
        const auto constant = constantOf(operand);
        if (!constant || !constant->isInteger() || constant->integer().hasUnknown()) return false;
        const auto bit = constant->integer().as<uint64_t>();
        return bit && *bit == want;
    };
    const auto isSource = [&](const slang::ast::Expression& operand) {
        if (leaf == nullptr) return !constantOf(operand) && stripConversions(operand).kind != slang::ast::ExpressionKind::IntegerLiteral;
        // a selected bit of a net is taken as the leaf: the caller has established that the leaf is the only non-constant input
        return isExactCopyOf(operand, leaf, driverOf) || isBitSelect(operand);
    };
    const uint64_t want = xorForm ? 1 : 0;
    return (isSource(binary.left()) && constantBit(binary.right(), want)) || (isSource(binary.right()) && constantBit(binary.left(), want));
}

// A net the analysis has no driver for (black box output, undriven, cyclic): its value and domain are not known.
bool hasNoModelledDriver(const Model& m, NodeId n) {
    if (n == kNoNode || n >= m.nodes.size()) return false;
    const RootKind kind = m.traceRoot(n).kind;
    return kind == RootKind::BlackBox || kind == RootKind::Undriven || kind == RootKind::Cycle;
}

// The synchronisation marker of one stage (RDC-030). Every attribute is read: the answer must not depend on their order.
// A value that is not a known 0/1 (X, Z or non-constant) is Unknown and counts as not marked unless another marker enables it.
enum class StageMark { Absent, Off, On, Unknown };

StageMark stageMark(const slang::ast::Compilation& compilation, const slang::ast::ValueSymbol& symbol) {
    bool on = false, off = false, unknown = false;
    auto& mutableCompilation = const_cast<slang::ast::Compilation&>(compilation);
    for (const auto* attribute : mutableCompilation.getAttributes(symbol)) {
        std::string name(attribute->name);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "async_reg" && name != "sverity_sync" && name != "cdc_sync") continue;
        // Only an integer or string value is read. Anything else (real, unpacked, unevaluated) is Unknown, never a throw.
        const auto& value = attribute->getValue();
        if (!value.isInteger() && !value.isString()) { unknown = true; continue; }
        if (value.isInteger() && value.integer().hasUnknown()) { unknown = true; continue; }
        if (value.isInteger() && value.integer() == 0) { off = true; continue; }
        // A string literal evaluates to its bits, so it is read back as text first ("FALSE" is a non-zero number).
        const slang::ConstantValue asText = value.isString() ? value : value.convertToStr();
        if (asText.isString()) {
            std::string text = asText.str();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            (text == "false" || text == "no" || text == "0" ? off : on) = true;
        } else {
            on = true;   // a non-zero number
        }
    }
    // Markers are independent: one enabling marker marks the stage whatever another marker on it says.
    if (on) return StageMark::On;
    if (unknown) return StageMark::Unknown;
    return off ? StageMark::Off : StageMark::Absent;
}
} // namespace

/// Walks a candidate reset synchroniser from its last stage back to the constant-D first stage: the RDC-003 chain facts.
SyncInfo syncChainOf(const Model& m, const AssertedUses& assertedUses, uint32_t last) {
    SyncInfo si;
    std::set<std::pair<NodeId, bool>> firstResets;
    std::set<uint32_t> seen;
    uint32_t cur = last;
    const auto& output = m.flops[last];
    if (m.procs[output.proc].bodyTiming) { si.why = "a stage has a timing control or delay"; return si; }
    char asserted = output.resetValues.size() == 1 ? output.resetValues[0] : '?';
    if (asserted != '0' && asserted != '1') { si.why = "asserted value is not binary"; return si; }
    // All users of this output must agree with its asserted level. Checking here keeps
    // source tracing, stage exemptions and the structure rules on the same predicate.
    auto uses = assertedUses.find(last);
    if (uses != assertedUses.end() && (uses->second.size() != 1 || !uses->second.count(asserted))) {
        si.why = "asserted value disagrees with downstream reset polarity"; return si;
    }
    for (int guard = 0; guard < 16; guard++) {
        const Flop& f = m.flops[cur];
        if (!seen.insert(cur).second) { si.why = "cyclic register chain"; return si; }
        if (m.procs[f.proc].bodyTiming) { si.why = "a stage has a timing control or delay"; return si; }
        if (f.blocking) { si.why = "a stage uses blocking assignments"; return si; }
        if (!f.clockResolved) { si.why = "a stage has no identified clock"; return si; }
        if (f.resets.empty()) { si.why = "a stage has no asynchronous reset/set (assertion would not be asynchronous)"; return si; }
        si.depth++;
        si.stages.push_back(cur);
        std::set<std::pair<NodeId, bool>> rr;
        for (auto& r : f.resets) rr.emplace(r.root.node, r.activeLow);
        if (si.depth == 1) firstResets = rr;
        else if (rr != firstResets) { si.why = "stages have different asynchronous resets or polarities"; return si; }
        std::vector<const Edge*> data;
        for (auto ei : m.in[f.q]) {
            const Edge& e = m.edges[ei];
            if (e.type == EdgeType::Seq) data.push_back(&e);
        }
        si.logic = data.size() > 1 || (data.size() == 1 && (!data[0]->exact || data[0]->ctrl));
        if (!f.resetTransitionKnown || f.resetValues.size() != 1 || f.resetValues[0] != asserted) {
            si.why = "a stage's assertion and release assignments are not established or have inconsistent asserted values"; return si;
        }
        char released = f.releasedSource == kNoNode ? f.releasedValue : m.constantValue(f.releasedSource);
        if (released == (asserted == '0' ? '1' : '0')) {
            si.recognised = true; si.first = cur; return si;
        }
        if (f.releasedSource == kNoNode || data.size() != 1 || !data[0]->exact || data[0]->ctrl) {
            si.why = "a stage's data input is not a plain previous stage or the released binary constant";
            si.logic = !data.empty(); return si;
        }
        uint32_t prev = 0;
        const Flop* pf = m.flopBehind(f.releasedSource, &prev);
        if (!pf || m.aliasRoot(data[0]->from) != m.aliasRoot(f.releasedSource)) {
            si.why = "the first stage is not fed the released binary constant"; return si;
        }
        if (!pf->clockResolved || pf->domain != f.domain || pf->edge != f.edge) {
            si.why = "stages are clocked by different clocks or edges"; return si;
        }
        cur = prev;
    }
    si.why = "chain longer than the analysis limit";
    return si;
}

AssertedUses assertedUsesOf(const Model& m) {
    AssertedUses uses;
    for (const auto& user : m.flops) for (const auto& r : user.resets) {
        uint32_t idx = 0;
        if (m.flopBehind(r.root.node, &idx)) uses[idx].insert(r.activeLow ? '0' : '1');
    }
    return uses;
}

model::ContractBindingResult bindContracts(const Model& m, const ClockRelations& rel, const Config& config, const AssertedUses& uses) {
    // The synchroniser depth a witness chain must reach is the RDC-006 policy (default 2), as RDC-006 itself reads it.
    int64_t minStages = 2;
    try {
        const std::string value = config.option("RDC-006.min_stages", "2");
        size_t end = 0;
        const int parsed = std::stoi(value, &end);
        if (end == value.size() && parsed >= 2) minStages = parsed;
    } catch (...) {}
    return bindResetContracts(m, rel, config.resetContracts, config.constraints, [&](uint32_t last) {
        const SyncInfo si = syncChainOf(m, uses, last);
        return model::SyncChainFacts{si.recognised && si.first != ~0u, si.why, si.stages, si.first};
    }, minStages);
}

const slang::ast::Expression* RdcShared::driverExpression(const slang::ast::Symbol* net) const {
    if (!driversIndexed_) {
        driversIndexed_ = true;
        for (const auto& fact : m_->pairOperatorFacts) {
            if (!fact.rhs || !fact.anchor || fact.left || fact.right) continue;
            const slang::ast::Symbol* key = nullptr;
            if (fact.anchor->kind == slang::ast::SymbolKind::ContinuousAssign) {
                const auto& lhs = fact.anchor->as<slang::ast::ContinuousAssignSymbol>().getAssignment().as<slang::ast::AssignmentExpression>().left();
                if (lhs.kind == slang::ast::ExpressionKind::NamedValue) key = lhs.getSymbolReference();
                else if (lhs.kind == slang::ast::ExpressionKind::ElementSelect || lhs.kind == slang::ast::ExpressionKind::RangeSelect) {
                    // `assign r[1] = ...`: the bits it drives are recorded for the bit-precise literal check
                    const slang::ast::Expression* value = lhs.kind == slang::ast::ExpressionKind::ElementSelect
                        ? &lhs.as<slang::ast::ElementSelectExpression>().value() : &lhs.as<slang::ast::RangeSelectExpression>().value();
                    if (value->kind == slang::ast::ExpressionKind::NamedValue && value->getSymbolReference() &&
                        value->getSymbolReference()->isValue() && value->getSymbolReference()->as<slang::ast::ValueSymbol>().getType().isIntegral()) {
                        // the bits are offsets from the LSB of the net, as the model numbers them: a declared index is translated by its range
                        // ([7:4] and [0:3] alike); an indexed select (`r[b+:w]`, `r[b-:w]`) names w indices from b
                        const auto& net = value->getSymbolReference()->as<slang::ast::ValueSymbol>();
                        const slang::ConstantRange declared = net.getType().getFixedRange();
                        const auto constantIndex = [](const slang::ast::Expression& e) -> std::optional<int32_t> {
                            const auto* c = e.getConstant();
                            if (!c || !c->isInteger() || c->integer().hasUnknown()) return std::nullopt;
                            return c->integer().as<int32_t>();
                        };
                        const auto offsetOf = [&](int32_t index) -> std::optional<uint64_t> {
                            if (!declared.containsPoint(index)) return std::nullopt;
                            return static_cast<uint64_t>(declared.translateIndex(index));
                        };
                        PartDriver part{0, UINT64_MAX, fact.rhs};
                        std::optional<int32_t> from, to;
                        if (lhs.kind == slang::ast::ExpressionKind::ElementSelect) {
                            from = to = constantIndex(lhs.as<slang::ast::ElementSelectExpression>().selector());
                        } else {
                            const auto& range = lhs.as<slang::ast::RangeSelectExpression>();
                            const auto left = constantIndex(range.left());
                            const auto rightOrWidth = constantIndex(range.right());   // the right index, or the width of an indexed select
                            using K = slang::ast::RangeSelectionKind;
                            if (range.getSelectionKind() == K::Simple) {
                                from = left;
                                to = rightOrWidth;
                            } else if (left && rightOrWidth && *rightOrWidth > 0) {
                                // `b+:w` names b .. b+w-1; `b-:w` names b-w+1 .. b
                                from = range.getSelectionKind() == K::IndexedUp ? *left : *left - *rightOrWidth + 1;
                                to = range.getSelectionKind() == K::IndexedUp ? *left + *rightOrWidth - 1 : *left;
                            }
                        }
                        if (from && to) {
                            const auto a = offsetOf(*from), b = offsetOf(*to);
                            if (a && b) {
                                part.lo = std::min(*a, *b);
                                part.hi = std::max(*a, *b);
                            }
                        }
                        partDrivers_[&net].push_back(part);
                    }
                }
            } else if (fact.anchor->isValue()) {
                key = fact.anchor;
            }
            if (key) driverByNet_.emplace(key, fact.rhs);
        }
    }
    const auto it = driverByNet_.find(net);
    return it == driverByNet_.end() ? nullptr : it->second;
}

bool RdcShared::unknownLiteralAtBit(const slang::ast::Symbol* net, uint64_t bit, bool whole) const {
    if (net == nullptr) return false;
    if (const auto* driver = driverExpression(net); driver && unknownLiteralReaches(*driver, whole ? std::nullopt : std::optional<uint64_t>(bit)))
        return true;
    // a part driver (`assign r[1:0] = ...`) reaches only the bits it names; its right side is read bit by bit from the part's LSB
    const auto it = partDrivers_.find(net);
    if (it != partDrivers_.end())
        for (const auto& part : it->second) {
            if (whole || part.hi == UINT64_MAX) {   // an index that is not constant: the whole right side counts
                if (containsUnknownLiteral(*part.rhs)) return true;
            } else if (bit >= part.lo && bit <= part.hi && unknownLiteralReaches(*part.rhs, bit - part.lo)) {
                return true;
            }
        }
    return false;
}

NodeId RdcShared::assertionSource(NodeId n) const {
    // Each hop is one recognised synchroniser (at most 16 stages). Past the hop bound the identity is not traced: the node is
    // kept as Unresolved (RDC-013) rather than taken for the source.
    constexpr int kMaxHops = 64;
    for (int hop = 0; hop <= kMaxHops; hop++) {
        uint32_t ri = 0;
        const Flop* rf = flopBehind(n, &ri);
        if (!rf) return n;
        SyncInfo si = syncChain(ri);
        if (!si.recognised || si.first == ~0u || m_->flops[si.first].resets.empty()) return n;
        // the node reached on the last allowed hop is inspected first: it is the source when it is not a stage itself
        if (hop == kMaxHops) break;
        n = m_->flops[si.first].resets[0].root.node;
    }
    // this trace is cut at a node that is still a synchroniser stage; only this trace is marked
    traceTruncated_.insert(n);
    return n;
}

NodeId RdcShared::copyRootOf(NodeId n) const {
    // one driver per hop: an exact copy or a bit-for-bit inversion, not approximate, shared or controlled
    for (int hop = 0; hop < 16; hop++) {
        if (n == kNoNode || n >= m_->in.size() || m_->in[n].size() != 1) return n;
        const Edge& e = m_->edges[m_->in[n][0]];
        if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) return n;
        n = e.from;
    }
    return n;
}

std::set<NodeId> RdcShared::asyncSources(const Flop& f) const {
    std::set<NodeId> out;
    // `posedge aux` with `wire aux = ~aux_n` and `negedge aux_n` assert on the same source: the copy's identity is its source
    for (auto& r : f.resets) out.insert(copyRootOf(assertionSource(r.root.node)));
    return out;
}

bool RdcShared::inputHasFromException(NodeId inputNode, const Config& cfg) const {
    if (inputNode == kNoNode || inputNode >= m_->nodes.size()) return false;
    const std::string inputFull = stripBit(m_->nameOf(inputNode));
    const auto& clocks = cfg.constraints.clocks;
    auto clockInForce = [&](size_t index) {
        for (size_t j = index + 1; j < clocks.size(); ++j) {
            if (clocks[j].add) continue;
            for (const auto& src : clocks[j].sources)
                if (rel_->clockSourceMatches(src, inputNode, inputFull)) return false;
        }
        return true;
    };
    // The nodes the input reaches through exact or inverted copies (internal nets, child-port pins): a selector naming any of them
    // covers the input.
    std::vector<NodeId> chain{inputNode};
    {
        std::unordered_set<NodeId> seen{inputNode};
        for (size_t i = 0; i < chain.size() && chain.size() < 256; ++i) {
            const NodeId c = chain[i];
            if (c >= m_->out.size()) continue;
            for (auto ei : m_->out[c]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb || !(e.exact || e.flip) || e.ctrl) continue;
                if (seen.insert(e.to).second) chain.push_back(e.to);
            }
        }
    }
    auto selectorHits = [&](const sdc::ObjectRef& ref) {
        if (ref.kind == "get_clocks") {
            for (size_t i = 0; i < clocks.size(); ++i) {
                for (const auto& source : clocks[i].sources) {
                    const std::string& clockName = clocks[i].name.empty() ? source.pattern : clocks[i].name;
                    if (globMatch(ref.pattern, clockName) && rel_->clockSourceMatches(source, inputNode, inputFull) && clockInForce(i))
                        return true;
                }
            }
            return false;
        }
        for (const NodeId n : chain) {
            const std::string full = n == inputNode ? inputFull : stripBit(m_->nameOf(n));
            if (rel_->nameMatches(ref.kind, ref.pattern, n, full) || rel_->bitMatches(ref.kind, ref.pattern, n)) return true;
        }
        return false;
    };
    for (const auto& ex : cfg.constraints.exceptions) {
        if (ex.kind != sdc::PathException::Kind::FalsePath && ex.kind != sdc::PathException::Kind::Multicycle) continue;
        for (const auto& ref : ex.from)
            if (selectorHits(ref)) return true;
        for (const auto& ref : ex.through)
            if (selectorHits(ref)) return true;
    }
    return false;
}

bool RdcShared::inputTimedToDomain(NodeId inputNode, uint32_t dom, const Config& cfg) const {
    if (inputNode == kNoNode || inputNode >= m_->topInput.size() || !m_->topInput[inputNode]) return false;
    if (inputHasFromException(inputNode, cfg)) return false;
    bool rejectedMode = false;
    const auto active = rel_->effectiveInputDelays(inputNode, &rejectedMode);
    if (rejectedMode || active.empty()) return false;
    for (const auto& c : active) {
        if (!c.delay || rel_->relationToName(dom, c.delay->clock) != Relation::Same)
            return false;
    }
    return true;
}

int32_t RdcShared::dataSyncLastStage(uint32_t flopIdx) const {
    if (flopIdx >= dataSyncLastMemo_.size()) return -1;
    if (dataSyncLastMemo_[flopIdx] != -2) return dataSyncLastMemo_[flopIdx];
    auto compute = [&]() -> int32_t {
        const Flop& firstStage = m_->flops[flopIdx];
        if (!firstStage.clockResolved || firstStage.blocking || firstStage.edge == 3 || firstStage.hasIff ||
            firstStage.syncDataSuppressed || !firstStage.otherDomains.empty() ||
            m_->multiSequential.count(firstStage.q) || cones_->hasUncertainCalls(flopIdx))
            return -1;
        auto isSyncResetGuard = [](const Flop& f, NodeId n) {
            return n != kNoNode && std::find(f.syncResetGuards.begin(), f.syncResetGuards.end(), n) != f.syncResetGuards.end();
        };
        auto asyncResetSig = [&](const Flop& f) {
            std::set<std::pair<NodeId, bool>> sig;
            for (const auto& r : f.resets) sig.emplace(copyRootOf(r.root.node), r.activeLow);
            return sig;
        };
        auto aliasForward = [&](NodeId start, std::unordered_set<NodeId>& out) {
            std::vector<NodeId> work{start};
            out.insert(start);
            while (!work.empty()) {
                NodeId n = work.back();
                work.pop_back();
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && e.exact && !e.ctrl && !m_->isDeadEdge(e) && !m_->isConstantProc(e.proc) &&
                        out.insert(e.to).second)
                        work.push_back(e.to);
                }
            }
        };
        // Stage 1 must not have self-feedback or a non-sync-reset control guard, and must have live data (or library stages).
        std::unordered_set<NodeId> firstClosure;
        aliasForward(firstStage.q, firstClosure);
        bool sawSeqData = false, sawLiveSeqData = false;
        for (auto ei : m_->in[firstStage.q]) {
            const Edge& e = m_->edges[ei];
            if (e.type != EdgeType::Seq) continue;
            if (e.ctrl && !isSyncResetGuard(firstStage, e.from)) return -1;
            if (!e.ctrl) {
                sawSeqData = true;
                if (!m_->isDeadEdge(e)) {
                    sawLiveSeqData = true;
                    if (firstClosure.count(e.from)) return -1;   // direct self-feedback
                }
            }
        }
        if (sawSeqData && !sawLiveSeqData) return -1;
        int depth = 1 + firstStage.extraStages;
        std::vector<uint32_t> stages{flopIdx};
        std::set<uint32_t> visited{flopIdx};
        uint32_t cur = flopIdx;
        const auto baseResets = asyncResetSig(firstStage);
        auto tryStepNext = [&](uint32_t fromIdx, bool optional) -> int32_t {
            const Flop& f = m_->flops[fromIdx];
            std::unordered_set<NodeId> cl;
            aliasForward(f.q, cl);
            std::map<NodeId, std::vector<const Edge*>> seqTargets;
            for (NodeId n : cl) {
                if (n < m_->topOutput.size() && m_->topOutput[n]) return -1;
                if (clockDomainRoots_.count(n)) return -1;
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                    if (e.type == EdgeType::AsyncCtl) return -1;
                    if (e.type == EdgeType::Comb) {
                        if (!(e.exact && !e.ctrl)) return -1;
                    } else if (e.type == EdgeType::Seq) {
                        seqTargets[e.to].push_back(&e);
                    }
                }
            }
            if (seqTargets.size() != 1) return -1;
            const auto& [tq, es] = *seqTargets.begin();
            auto it = m_->flopOfQ.find(tq);
            if (it == m_->flopOfQ.end() || visited.count(it->second)) return -1;
            const uint32_t nextIdx = it->second;
            const Flop& next = m_->flops[nextIdx];
            if (!next.clockResolved || next.blocking || next.hasIff || next.edge == 3 ||
                !next.otherDomains.empty() || m_->multiSequential.count(next.q) || cones_->hasUncertainCalls(nextIdx))
                return -1;
            if (rel_->relation(next.domain, f.domain) != Relation::Same || next.edge != f.edge) return -1;
            if (asyncResetSig(next) != baseResets) return -1;
            if (optional && (next.syncResetGuards != firstStage.syncResetGuards || !next.extraResetGuards.empty()))
                return -1;
            for (const Edge* e : es) if (!e->exact || e->ctrl) return -1;
            for (auto ei : m_->in[tq]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
                if (e.ctrl && isSyncResetGuard(next, e.from)) continue;
                if (!cl.count(e.from)) return -1;
            }
            return static_cast<int32_t>(nextIdx);
        };
        while (depth < cdcMinStages_) {
            const int32_t nextIdx = tryStepNext(cur, false);
            if (nextIdx < 0) return -1;
            const uint32_t uNext = static_cast<uint32_t>(nextIdx);
            depth += 1 + m_->flops[uNext].extraStages;
            stages.push_back(uNext);
            visited.insert(uNext);
            cur = uNext;
        }
        // Extend through any further clean synchroniser stages (e.g. 3-stage sync) to find the true last stage.
        while (stages.size() < 16) {
            const int32_t nextIdx = tryStepNext(cur, true);
            if (nextIdx < 0) break;
            const uint32_t uNext = static_cast<uint32_t>(nextIdx);
            depth += 1 + m_->flops[uNext].extraStages;
            stages.push_back(uNext);
            visited.insert(uNext);
            cur = uNext;
        }
        // A feedback sync-reset guard from a later stage changes capture and is not a clean synchroniser.
        if (!firstStage.syncResetGuards.empty()) {
            for (size_t i = 1; i < stages.size(); ++i) {
                const NodeId laterQ = m_->flops[stages[i]].q;
                if (std::any_of(firstStage.syncResetGuards.begin(), firstStage.syncResetGuards.end(), [&](NodeId g) {
                        return g == laterQ || m_->aliasRoot(g) == laterQ;
                    }))
                    return -1;
            }
        }
        return depth >= cdcMinStages_ ? static_cast<int32_t>(cur) : -1;
    };
    const int32_t res = compute();
    dataSyncLastMemo_[flopIdx] = res;
    return res;
}

void RdcShared::structure(RuleContext& ctx) {
    for (const char* r : {"RDC-002", "RDC-004", "RDC-005", "RDC-008", "RDC-009", "RDC-010", "RDC-011", "RDC-012", "RDC-013", "RDC-014", "RDC-016",
                          "RDC-017", "RDC-019", "RDC-020", "RDC-025", "RDC-026", "RDC-027", "RDC-028", "RDC-029", "RDC-030"})
        evaluated[r] += 0;
    auto flopName = [&](const Flop& f) { return m_->nameOf(f.q); };
    auto procLoc = [&](const Flop& f) -> const ProcInfo& { return m_->procs[f.proc]; };
    auto isUnreset = [&](const Flop& f) { return !resetsBit(f) && !f.hasSyncResetCandidate; };
    // A reset entry whose forced value is not a binary constant (`'?'`, `'x'`, `'z'`) leaves the reset extraction unresolved, whatever its kind.
    for (auto& f : m_->flops)
        for (size_t k = 0; k < f.resets.size() && k < f.resetValues.size(); k++) {
            const char rv = f.resetValues[k];
            if (rv != '?' && rv != 'x' && rv != 'z') continue;
            ctx.report.markIncomplete("reset-extraction", "asynchronous reset '" + stripBit(m_->nameOf(f.resets[k].root.node)) + "' of register " +
                                      stripBit(flopName(f)) + " forces a value that is not a constant (case-form, conditional guard or conflicting assignment)");
            k = f.resets.size();   // one reason is enough for the scope
        }

    // distinct non-tied asynchronous reset roots and their users (constant-tied resets never assert/release dynamically)
    std::map<NodeId, std::vector<uint32_t>> usersOf;
    std::set<NodeId> sources;
    for (uint32_t i = 0; i < m_->flops.size(); i++)
        for (auto& r : m_->flops[i].resets) {
            const char tied = tiedResetValue(*m_, r.root.node);
            if (tied == '0' || tied == '1') continue;
            usersOf[r.root.node].push_back(i);
            sources.insert(copyRootOf(assertionSource(copyRootOf(r.root.node))));
        }
    std::set<NodeId> resetRoots;
    for (auto& [n, u] : usersOf) resetRoots.insert(n);

    // --- registers with an asynchronous reset but no identified clock are skipped by the clock-domain based reset checks below:
    // say so, never leave them without a finding (the clock itself is CDC-001 / CST-004)
    {
        std::set<std::string> seenUnidentified;
        for (auto& f : m_->flops) {
            if (f.clockResolved || f.resets.empty() || !seenUnidentified.insert(stripBit(flopName(f))).second) continue;
            evaluated["RDC-003"]++;
            Diagnostic d = mk("RDC-003", Severity::Warning, "register " + stripBit(flopName(f)) + " has an asynchronous reset but no identified clock: reset deassertion synchronisation and reset-domain crossings "
                              "for it are unresolved", procLoc(f).loc, procLoc(f).instancePath, Confidence::Unresolved);
            d.signals = {stripBit(flopName(f))};
            add(std::move(d));
            ctx.report.markIncomplete("reset-extraction", "register " + stripBit(flopName(f)) + " has a reset but no identified clock");
        }
    }

    // Clauses of the reset_contracts binding that name one of these exact reset nodes, cited once each in config order. Evidence only:
    // a cited clause never changes the severity, count or identity of the finding.
    const auto citeSiteClauses = [&](Diagnostic& d, std::initializer_list<const std::set<NodeId>*> sites) {
        std::set<size_t> indices;
        for (const auto* set : sites)
            for (NodeId n : *set)
                for (const auto* c : contractsOfBit(contracts_, contractBitName(*m_, *rel_, n)))
                    indices.insert(static_cast<size_t>(c - contracts_.entries.data()));
        for (size_t i : indices) citeClause(d, contracts_.entries[i]);
    };

    // Copy/inversion trace returning (root node, odd inversion parity).
    const auto copyRootWithFlip = [&](NodeId n) -> std::pair<NodeId, bool> {
        bool flipped = false;
        for (int hop = 0; hop < 16; hop++) {
            if (n == kNoNode || n >= m_->in.size() || m_->in[n].size() != 1) return {n, flipped};
            const Edge& e = m_->edges[m_->in[n][0]];
            if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) return {n, flipped};
            if (e.flip) flipped = !flipped;
            n = e.from;
        }
        return {n, flipped};
    };
    // Assertion source through recognised reset synchronisers and exact/inverted copies, tracking effective active-low polarity.
    const auto assertionSourceWithPolarity = [&](NodeId n, bool activeLow) -> std::pair<NodeId, bool> {
        return assertionSourceOf(*m_, assertedUses_, n, activeLow);
    };
    using ResetKey = std::pair<NodeId, bool>;
    const auto asyncSourcesWithPolarity = [&](const Flop& f) -> std::set<ResetKey> {
        std::set<ResetKey> out;
        for (const auto& r : f.resets) {
            const char tied = tiedResetValue(*m_, r.root.node);
            if (tied == '0' || tied == '1') continue;
            out.insert(assertionSourceWithPolarity(r.root.node, r.activeLow));
        }
        return out;
    };
    const auto hasTiedInactiveReset = [&](const Flop& f) -> bool {
        if (!asyncSourcesWithPolarity(f).empty()) return false;
        for (const auto& r : f.resets) {
            const char tied = tiedResetValue(*m_, r.root.node);
            if (tied == (r.activeLow ? '1' : '0')) return true;
        }
        return false;
    };
    const auto hasTiedActiveReset = [&](const Flop& f) -> bool {
        for (size_t ri = 0; ri < f.resets.size(); ++ri) {
            const auto& r = f.resets[ri];
            const char tied = tiedResetValue(*m_, r.root.node);
            if (tied == (r.activeLow ? '0' : '1')) {
                const char rv = ri < f.resetValues.size() ? f.resetValues[ri] : '?';
                if (rv == '0' || rv == '1') return true;
            }
        }
        return false;
    };
    // Whether asserting source reset (srcRoot, srcActiveLow) triggers guard (guardNode, guardActiveLow):
    // directly, through exact/inverted copies, through a recognised reset synchroniser, or through a clean
    // synchronised register chain (>= cdcMinStages_ same-clock, same-edge nonblocking flops) sampling srcRoot.
    const auto guardCoversSourceReset = [&](NodeId guardNode, bool guardActiveLow, NodeId srcRoot, bool srcActiveLow) -> bool {
        if (guardNode == kNoNode || srcRoot == kNoNode) return false;
        auto [n, flipped] = copyRootWithFlip(guardNode);
        const bool needLow = guardActiveLow ^ flipped;
        if (n == srcRoot && needLow == srcActiveLow) return true;
        // Recognised reset synchroniser output matching asserted level and raw source.
        uint32_t ri = 0;
        if (const Flop* rf = flopBehind(n, &ri)) {
            SyncInfo si = syncChain(ri);
            if (si.recognised && !rf->resetValues.empty() && (rf->resetValues[0] == '0' || rf->resetValues[0] == '1')) {
                const bool rfAssertedLow = rf->resetValues[0] == '0';
                if (needLow == rfAssertedLow) {
                    auto [rawRoot, rawLow] = assertionSourceWithPolarity(n, rfAssertedLow);
                    if (rawRoot == srcRoot && rawLow == srcActiveLow) return true;
                }
            }
        }
        // Synchronised register chain (e.g. ra1_q -> ra_b_q sampling rst_a_n) feeding the guard.
        auto fit = m_->flopOfQ.find(n);
        if (fit == m_->flopOfQ.end()) return false;
        const uint32_t lastIdx = fit->second;
        const Flop& lastF = m_->flops[lastIdx];
        if (!lastF.clockResolved || lastF.blocking || lastF.edge == 3 || lastF.hasIff ||
            lastF.syncDataSuppressed || !lastF.otherDomains.empty() || m_->multiSequential.count(lastF.q))
            return false;
        uint32_t cur = lastIdx;
        bool chainFlipped = false;
        int chainDepth = 0;
        std::set<uint32_t> seenFlops;
        while (seenFlops.insert(cur).second && chainDepth < 16) {
            const Flop& cf = m_->flops[cur];
            if (!cf.clockResolved || cf.blocking || cf.edge == 3 || cf.hasIff ||
                cf.syncDataSuppressed || !cf.otherDomains.empty() || m_->multiSequential.count(cf.q))
                break;
            if (rel_->relation(cf.domain, lastF.domain) != Relation::Same || cf.edge != lastF.edge)
                break;
            chainDepth += 1 + cf.extraStages;
            NodeId dataIn = kNoNode;
            bool dataFlip = false;
            bool ok = true;
            for (auto ei : m_->in[cf.q]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
                if (e.ctrl) {
                    if (std::find(cf.syncResetGuards.begin(), cf.syncResetGuards.end(), e.from) != cf.syncResetGuards.end())
                        continue;
                    ok = false; break;
                }
                if (!(e.exact || e.flip)) { ok = false; break; }
                if (dataIn != kNoNode && (dataIn != e.from || dataFlip != e.flip)) { ok = false; break; }
                dataIn = e.from;
                dataFlip = e.flip;
            }
            if (!ok || dataIn == kNoNode) break;
            auto [srcNode, combFlip] = copyRootWithFlip(dataIn);
            const bool totalFlip = chainFlipped ^ dataFlip ^ combFlip;
            if (chainDepth >= cdcMinStages_) {
                if (srcNode == srcRoot && (needLow ^ totalFlip) == srcActiveLow) return true;
                uint32_t sri = 0;
                if (const Flop* srf = flopBehind(srcNode, &sri)) {
                    SyncInfo ssi = syncChain(sri);
                    if (ssi.recognised && !srf->resetValues.empty() && (srf->resetValues[0] == '0' || srf->resetValues[0] == '1')) {
                        const bool srfAssertedLow = srf->resetValues[0] == '0';
                        if ((needLow ^ totalFlip) == srfAssertedLow) {
                            auto [rawRoot, rawLow] = assertionSourceWithPolarity(srcNode, srfAssertedLow);
                            if (rawRoot == srcRoot && rawLow == srcActiveLow) return true;
                        }
                    }
                }
            }
            auto pit = m_->flopOfQ.find(srcNode);
            if (pit == m_->flopOfQ.end()) break;
            chainFlipped = totalFlip;
            cur = pit->second;
        }
        return false;
    };
    const auto consumerResetBySource = [&](const Flop& consumer, NodeId srcRoot, bool srcActiveLow) -> bool {
        // 1. Asynchronous resets (direct, synchroniser, synchronised copy, or uninverted AND-for-low / OR-for-high combination).
        for (size_t ri = 0; ri < consumer.resets.size(); ++ri) {
            const char rv = ri < consumer.resetValues.size() ? consumer.resetValues[ri] : '?';
            if (rv != '0' && rv != '1') continue;
            const auto& r = consumer.resets[ri];
            if (guardCoversSourceReset(r.root.node, r.activeLow, srcRoot, srcActiveLow)) return true;
            // Uninverted active-low AND / active-high OR combination: any matching leaf forces assertion.
            const Op wantOp = r.activeLow ? Op::And : Op::Or;
            std::vector<NodeId> work{r.root.node};
            std::unordered_set<NodeId> seen{r.root.node};
            for (size_t wi = 0; wi < work.size() && seen.size() < 32; ++wi) {
                const NodeId u = work[wi];
                if (u != r.root.node && guardCoversSourceReset(u, r.activeLow, srcRoot, srcActiveLow)) return true;
                if (u >= m_->in.size() || m_->flopOfQ.count(u)) continue;
                bool allOk = !m_->in[u].empty();
                std::vector<NodeId> nexts;
                for (auto ei : m_->in[u]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type != EdgeType::Comb || e.ctrl || e.approximate) { allOk = false; break; }
                    if (e.exact && e.inv == 0) nexts.push_back(e.from);
                    else if (!e.exact && e.op == wantOp && e.inv == 0) nexts.push_back(e.from);
                    else { allOk = false; break; }
                }
                if (allOk) for (NodeId nx : nexts) if (seen.insert(nx).second) work.push_back(nx);
            }
        }
        // 2. Synchronous reset facts (for flops without async reset) and OR-combined / immediate else-if reset guards.
        for (const auto& fact : consumer.syncResetFacts) {
            if ((fact.forcedValue == '0' || fact.forcedValue == '1') &&
                guardCoversSourceReset(fact.node, fact.activeLow, srcRoot, srcActiveLow))
                return true;
        }
        for (const auto& fact : consumer.extraResetGuards) {
            if ((fact.forcedValue == '0' || fact.forcedValue == '1') &&
                guardCoversSourceReset(fact.node, fact.activeLow, srcRoot, srcActiveLow))
                return true;
        }
        return false;
    };
    const auto consumersResetBySource = [&](uint32_t lastStageIdx, const Flop& sf, const Flop& d) -> bool {
        const auto dstSources = asyncSourcesWithPolarity(d);
        std::vector<std::pair<NodeId, bool>> missing;
        for (const auto& r : sf.resets) {
            const char tied = tiedResetValue(*m_, r.root.node);
            if (tied == '0' || tied == '1') continue;
            auto key = assertionSourceWithPolarity(r.root.node, r.activeLow);
            if (dstSources.count(key)) continue;
            missing.push_back(key);
        }
        if (missing.empty()) return true;
        // Walk forward from the synchroniser's last stage through combinational logic to all consuming registers.
        const NodeId startQ = m_->flops[lastStageIdx].q;
        std::vector<NodeId> work{startQ};
        std::unordered_set<NodeId> seen{startQ};
        std::vector<uint32_t> consumers;
        std::unordered_set<uint32_t> seenConsumers;
        for (size_t wi = 0; wi < work.size(); ++wi) {
            if (seen.size() > 4096) return false;
            const NodeId u = work[wi];
            if ((u < m_->topOutput.size() && m_->topOutput[u]) || blackBoxNodes_.count(u) || clockDomainRoots_.count(u))
                return false;
            if (u >= m_->out.size()) continue;
            for (auto ei : m_->out[u]) {
                const Edge& e = m_->edges[ei];
                if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                if (e.type == EdgeType::Comb) {
                    if (seen.insert(e.to).second) work.push_back(e.to);
                } else if (e.type == EdgeType::Seq || e.type == EdgeType::AsyncCtl) {
                    auto fit = m_->flopOfQ.find(e.to);
                    if (fit == m_->flopOfQ.end()) return false;
                    if (seenConsumers.insert(fit->second).second) consumers.push_back(fit->second);
                }
            }
        }
        if (consumers.empty()) return false;
        for (uint32_t ci : consumers) {
            const Flop& consumer = m_->flops[ci];
            for (const auto& [srcRoot, srcLow] : missing)
                if (!consumerResetBySource(consumer, srcRoot, srcLow)) return false;
        }
        return true;
    };

    // Shared functional-use predicate (RDC-019, and also required absent by RDC-002 / RDC-027's synchroniser-only exemption):
    // walks exact/inverted/polarity-adapter copies of `root` and detects any non-AsyncCtl out-edge (synchronous reset,
    // data, or enable), excluding a direct/inverted exact sole data input to a recognised CDC data-synchroniser first stage.
    // When `exemptInputDelayDomain` is true (RDC-019 only), functional uses in a clock domain C timed by an applied
    // set_input_delay on the root's top-level input (with no -from false-path/multicycle exception) are exempt.
    const auto hasFunctionalUse = [&](NodeId root, std::vector<std::string>* funcUsersOut = nullptr, bool exemptInputDelayDomain = false) -> bool {
        const NodeId delayInput = exemptInputDelayDomain ? copyRootOf(root) : kNoNode;
        bool hasAppliedInputDelay = false;
        if (delayInput != kNoNode && delayInput < m_->topInput.size() && m_->topInput[delayInput] &&
            !inputHasFromException(delayInput, ctx.config)) {
            bool rejectedMode = false;
            const auto active = rel_->effectiveInputDelays(delayInput, &rejectedMode);
            hasAppliedInputDelay = !rejectedMode && !active.empty();
        }
        bool functional = false;
        std::unordered_set<std::string> seenFuncUsers;
        const std::string rootName = stripBit(m_->nameOf(root));
        auto pushFuncUser = [&](const std::string& name) {
            if (!funcUsersOut || name.empty() || name == "<unknown>" || name == rootName || funcUsersOut->size() >= 16) return;
            if (seenFuncUsers.insert(name).second) funcUsersOut->push_back(name);
        };
        std::vector<NodeId> moved{root};
        std::unordered_set<NodeId> movedSeen{root};
        // A wired net that another driver also holds (a constant on a wand) is not a copy of the reset: the move is not followed.
        const auto plainCopy = [&](NodeId n) { return m_->in[n].size() == 1 && !m_->constDrivers.count(n); };
        for (size_t mi = 0; mi < moved.size(); mi++) {
            for (auto ei : m_->out[moved[mi]]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::AsyncCtl) continue;
                if (e.type == EdgeType::Comb && e.exact && !e.ctrl) {
                    if (plainCopy(e.to) && movedSeen.insert(e.to).second) moved.push_back(e.to);
                    continue;
                }
                // a bit-for-bit inversion (`wire inv = ~r`) is followed the same way: its uses decide whether it is functional
                if (e.type == EdgeType::Comb && e.flip && !e.ctrl && !e.approximate && !e.shared && plainCopy(e.to)) {
                    if (movedSeen.insert(e.to).second) moved.push_back(e.to);
                    continue;
                }
                // a constant XOR/XNOR (`wire rst = rst_n ^ 1'b1`, `rst_n ^ ACTIVE_LOW`) is a polarity adapter: it is followed the same
                // way, so a reset routed only to asynchronous pins through it is reset routing, not a functional use
                if (e.type == EdgeType::Comb && !e.ctrl && plainCopy(e.to) && m_->nodes[e.to].sym && m_->nodes[moved[mi]].sym) {
                    const slang::ast::Expression* adapter = driverExpression(m_->nodes[e.to].sym);
                    const auto driverOf = [&](const slang::ast::Symbol* net) { return driverExpression(net); };
                    if (adapter && invertingConstantForm(*adapter, m_->nodes[moved[mi]].sym, driverOf)) {
                        if (movedSeen.insert(e.to).second) moved.push_back(e.to);
                        continue;
                    }
                }
                // A reset sampled as data directly (or inverted) into the first stage of a recognised CDC data synchroniser
                // is deliberate resynchronisation of a reset level into another domain: exempt this edge.
                if (e.type == EdgeType::Seq && !e.ctrl && (e.exact || e.flip) && !e.approximate && !m_->isDeadEdge(e)) {
                    auto fit = m_->flopOfQ.find(e.to);
                    if (fit != m_->flopOfQ.end() && isDataSyncFirstStage(fit->second)) {
                        bool soleData = true;
                        for (auto inEi : m_->in[e.to]) {
                            const Edge& inE = m_->edges[inEi];
                            if (inE.type != EdgeType::Seq || inE.ctrl || m_->isDeadEdge(inE)) continue;
                            if (inE.from != moved[mi]) { soleData = false; break; }
                        }
                        if (soleData) continue;
                    }
                }
                // Input-delay trust model (RDC-019): functional uses in a clock domain C timed by the input's
                // applied set_input_delay are timed by STA against C, whereas uses in other domains still warn.
                if (hasAppliedInputDelay) {
                    if (e.type == EdgeType::Seq) {
                        auto fit = m_->flopOfQ.find(e.to);
                        if (fit != m_->flopOfQ.end() && m_->flops[fit->second].clockResolved &&
                            inputTimedToDomain(delayInput, m_->flops[fit->second].domain, ctx.config))
                            continue;
                    } else if (e.type == EdgeType::Comb) {
                        std::vector<NodeId> work{e.to};
                        std::unordered_set<NodeId> seen{e.to};
                        bool allTimed = true;
                        bool sawTarget = false;
                        for (size_t wi = 0; wi < work.size() && allTimed; ++wi) {
                            if (seen.size() > 256) { allTimed = false; break; }
                            const NodeId u = work[wi];
                            if ((u < m_->topOutput.size() && m_->topOutput[u]) || blackBoxNodes_.count(u) || clockDomainRoots_.count(u)) {
                                allTimed = false;
                                break;
                            }
                            if (auto fit = m_->flopOfQ.find(u); fit != m_->flopOfQ.end()) {
                                sawTarget = true;
                                if (!m_->flops[fit->second].clockResolved || !inputTimedToDomain(delayInput, m_->flops[fit->second].domain, ctx.config))
                                    allTimed = false;
                                continue;
                            }
                            if (u >= m_->out.size()) continue;
                            for (auto oei : m_->out[u]) {
                                const Edge& oe = m_->edges[oei];
                                if (m_->isDeadEdge(oe) || m_->isConstantProc(oe.proc)) continue;
                                if (oe.type == EdgeType::Seq) {
                                    auto fit = m_->flopOfQ.find(oe.to);
                                    if (fit == m_->flopOfQ.end() || !m_->flops[fit->second].clockResolved ||
                                        !inputTimedToDomain(delayInput, m_->flops[fit->second].domain, ctx.config)) {
                                        allTimed = false;
                                        break;
                                    }
                                    sawTarget = true;
                                } else if (oe.type == EdgeType::Comb) {
                                    if (seen.insert(oe.to).second) work.push_back(oe.to);
                                }
                            }
                        }
                        if (allTimed && sawTarget) continue;
                    }
                }
                functional = true;
                if (!funcUsersOut) return true;
                if (e.type == EdgeType::Seq) {
                    pushFuncUser(stripBit(m_->nameOf(e.to)));
                } else if (e.type == EdgeType::Comb) {
                    // Find the first downstream register(s) or top-level output(s) reached through combinational logic.
                    std::vector<NodeId> work{e.to};
                    std::unordered_set<NodeId> seen{e.to};
                    bool foundDownstream = false;
                    for (size_t wi = 0; wi < work.size() && seen.size() < 256; wi++) {
                        const NodeId u = work[wi];
                        if (m_->flopOfQ.count(u)) {
                            pushFuncUser(stripBit(m_->nameOf(u)));
                            foundDownstream = true;
                            continue;
                        }
                        if (u < m_->topOutput.size() && m_->topOutput[u]) {
                            pushFuncUser(stripBit(m_->nameOf(u)));
                            foundDownstream = true;
                        }
                        if (u >= m_->out.size()) continue;
                        for (auto oei : m_->out[u]) {
                            const Edge& oe = m_->edges[oei];
                            if (oe.type == EdgeType::Seq) {
                                pushFuncUser(stripBit(m_->nameOf(oe.to)));
                                foundDownstream = true;
                            } else if (oe.type == EdgeType::Comb && seen.insert(oe.to).second) {
                                work.push_back(oe.to);
                            }
                        }
                    }
                    if (!foundDownstream) pushFuncUser(stripBit(m_->nameOf(e.to)));
                }
            }
        }
        return functional;
    };

    // (2) Top-level reset input bit whose every asynchronous-reset use is a stage of a recognised reset synchroniser
    // (reached directly or through exact/inverted copies only, never through combinational/gated reset logic) and
    // which has no functional use (no synchronous-reset guard, data, or enable out-edge).
    std::unordered_map<NodeId, bool> syncOnlyResetMemo;
    // Collect top-level inputs that feed a combinational/gated reset root (not an exact/inverted copy of the input).
    std::unordered_set<NodeId> nonCopyResetInputs;
    for (auto root : resetRoots) {
        if (root < m_->topInput.size() && m_->topInput[root]) continue;
        const NodeId copyRoot = copyRootOf(root);
        if (copyRoot < m_->topInput.size() && m_->topInput[copyRoot]) continue;
        std::set<NodeId> seen;
        std::vector<NodeId> work{root};
        size_t visits = 0;
        while (!work.empty()) {
            NodeId n = work.back(); work.pop_back();
            if (!seen.insert(n).second || ++visits > 64 || n >= m_->nodes.size()) continue;
            if (n < m_->topInput.size() && m_->topInput[n]) {
                nonCopyResetInputs.insert(n);
                continue;
            }
            if (m_->flopAt(n)) continue;
            if (n >= m_->in.size()) continue;
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::Comb) work.push_back(e.from);
            }
        }
    }
    const auto isSyncOnlyResetInput = [&](NodeId input) -> bool {
        if (input == kNoNode || input >= m_->topInput.size() || !m_->topInput[input]) return false;
        auto [it, fresh] = syncOnlyResetMemo.try_emplace(input, false);
        if (!fresh) return it->second;
        if (nonCopyResetInputs.count(input)) return false;
        if (hasFunctionalUse(input)) return false;
        bool hasSyncUse = false;
        for (const auto& [root, users] : usersOf) {
            if (copyRootOf(root) != input) continue;
            for (uint32_t ui : users) {
                if (!fullSyncStages_.count(ui)) return false;
                hasSyncUse = true;
            }
        }
        it->second = hasSyncUse;
        return hasSyncUse;
    };

    // A top-level reset input with no functional use whose every asynchronous-reset user is a stage of a reset synchroniser recognised by chain
    // shape, or sits in a group whose release is safe at the first edge (rdc_release.cpp).
    const auto isReleaseSafeInput = [&](NodeId input) -> bool {
        if (input == kNoNode || input >= m_->topInput.size() || !m_->topInput[input]) return false;
        if (nonCopyResetInputs.count(input) || hasFunctionalUse(input)) return false;
        bool any = false;
        for (const auto& [root, users] : usersOf) {
            if (copyRootOf(root) != input) continue;
            for (uint32_t ui : users) {
                if (!shapeSyncStages_.count(ui) && !releaseSafeFlops_.count(ui) && !isReleaseFlop(*m_, assertedUses_, minStages_, ui)) return false;
                any = true;
            }
        }
        return any;
    };

    // --- RDC-004 / RDC-005: data leaving a register that can be reset asynchronously into one that is not reset by the same source.
    // One finding per destination register and pair of (leaf, effective assertion polarity) reset sets.
    struct ResetMismatch {
        const Flop* dst = nullptr;
        std::set<ResetKey> sourceResets, destinationResets;
        Relation relation = Relation::Same;
        uint32_t launch = 0, capture = 0;
        int launchEdge = 0;
        std::map<std::string, const Flop*> sources;
        uint32_t firstDestination = UINT32_MAX;                              // lowest register index seen with this key
        std::map<std::string, std::pair<std::pair<uint32_t, NodeId>, const Flop*>> firstSource;   // per name: lowest (destination, Q)
    };
    std::map<std::string, ResetMismatch> mismatches;
    // A source whose reset sources are all among the destination's is never read: the cones are walked without such sources.
    std::map<std::set<ResetKey>, uint32_t> resetSetIds;
    std::vector<const std::set<ResetKey>*> resetSets;
    const auto resetSetId = [&](std::set<ResetKey> sources) {
        auto [it, fresh] = resetSetIds.try_emplace(std::move(sources), static_cast<uint32_t>(resetSets.size()));
        if (fresh) resetSets.push_back(&it->first);
        return it->second;
    };
    // Each register's reset set id, computed once (kIgnore: no identified clock or no non-tied asynchronous reset).
    std::vector<uint32_t> resetSetOf(m_->flops.size(), SourceClasses::kIgnore);
    for (uint32_t fi = 0; fi < m_->flops.size(); fi++) {
        const Flop& f = m_->flops[fi];
        if (!f.clockResolved) continue;
        auto sources = asyncSourcesWithPolarity(f);
        if (!sources.empty()) resetSetOf[fi] = resetSetId(std::move(sources));
    }
    const auto resetClasses = cones_->classify([&](uint32_t fi) { return resetSetOf[fi]; }, false);
    // Names are built once per register and reset set: a whole SoC pairs millions of sources with their destinations.
    std::vector<std::string> flopNames(m_->flops.size());
    std::vector<uint8_t> flopNamed(m_->flops.size(), 0);
    const auto strippedName = [&](uint32_t fi) -> const std::string& {
        if (!flopNamed[fi]) { flopNames[fi] = stripBit(m_->nameOf(m_->flops[fi].q)); flopNamed[fi] = 1; }
        return flopNames[fi];
    };
    const auto resetBitName = [&](NodeId n) -> std::string {
        const std::string bn = rel_->rtlBitName(n);
        return bn.empty() ? m_->nameOf(n) : bn;
    };
    std::deque<std::string> setNames;   // references stay valid as names are added
    const auto resetSetNames = [&](uint32_t id) -> const std::string& {
        while (setNames.size() <= id) {
            std::string names;
            for (const auto& [n, low] : *resetSets[setNames.size()])
                names += resetBitName(n) + (low ? "(L)," : "(H),");
            setNames.push_back(std::move(names));
        }
        return setNames[id];
    };
    std::unordered_map<uint64_t, bool> coveredBy;
    const auto covers = [&](uint32_t own, uint32_t set) {   // the reset set `set` is a subset of `own`
        auto [it, fresh] = coveredBy.try_emplace(uint64_t{set} << 32 | own, true);
        if (fresh) it->second = std::includes(resetSets[own]->begin(), resetSets[own]->end(), resetSets[set]->begin(), resetSets[set]->end());
        return it->second;
    };
    std::unordered_map<uint64_t, bool> classCoveredBy;
    const auto classCovered = [&](uint32_t set, uint32_t own) {
        auto [it, fresh] = classCoveredBy.try_emplace(uint64_t{set} << 32 | own, true);
        if (fresh)
            for (const uint32_t cls : resetClasses.classes(set))
                if (!covers(own, cls)) { it->second = false; break; }
        return it->second;
    };
    // Whether a reset root is a recognised reset source (TopPort, exact/inverted copy of a TopPort,
    // or a recognised reset synchroniser whose raw sources trace through exact/inverted copies and
    // synchronisers to TopPorts). Data-register or logic-built resets are NOT recognised reset sources.
    const auto isRecognisedResetSource = [&](NodeId resetRootNode) -> bool {
        if (resetRootNode == kNoNode) return false;
        std::vector<std::pair<NodeId, int>> work{{resetRootNode, 0}};
        std::unordered_set<NodeId> seen;
        while (!work.empty()) {
            auto [cur, hop] = work.back();
            work.pop_back();
            if (!seen.insert(cur).second) continue;
            const NodeId copied = copyRootOf(cur);
            const Root r = m_->traceRoot(copied);
            if (r.kind == RootKind::TopPort) continue;
            if (r.kind != RootKind::Logic || hop >= 64) return false;
            uint32_t ri = 0;
            const Flop* rf = flopBehind(r.node, &ri);
            if (!rf && !m_->flopOfQ.count(r.node)) {
                // A combinational root (a scan mux over a synchroniser): every leaf of its fan-in must be a top input or a
                // recognised reset source; any other register, black box or undriven leaf rejects it.
                // A combinational loop in the cone rejects it (depth-first search for a back edge).
                {
                    std::unordered_map<NodeId, uint8_t> colour;   // 1 on the path, 2 finished
                    std::vector<std::pair<NodeId, size_t>> path{{r.node, 0}};
                    colour[r.node] = 1;
                    while (!path.empty()) {
                        auto& [n, idx] = path.back();
                        if (m_->flopOfQ.count(n) || n >= m_->in.size() || idx >= m_->in[n].size()) { colour[n] = 2; path.pop_back(); continue; }
                        const Edge& e = m_->edges[m_->in[n][idx++]];
                        if (e.type != EdgeType::Comb) continue;
                        const uint8_t c = colour[e.from];
                        if (c == 1) return false;
                        if (c == 0) { colour[e.from] = 1; path.push_back({e.from, 0}); }
                    }
                }
                // A top-input leaf in the cone of a mux select is a test-mode select and is trusted.
                std::unordered_set<uint64_t> walked{uint64_t{r.node} << 1};
                std::vector<std::pair<NodeId, bool>> st{{r.node, false}};
                while (!st.empty()) {
                    const auto [n, inSelect] = st.back();
                    st.pop_back();
                    if (m_->flopOfQ.count(n)) { work.emplace_back(n, hop + 1); continue; }
                    if (m_->traceRoot(n).kind == RootKind::Cycle) return false;
                    if (n >= m_->in.size() || m_->in[n].empty()) return false;
                    // A select-only top input that is not a KNOWN reset is trusted only on a mux with at least two data legs: with one
                    // leg the other is a constant or missing (`x ? s2 : 1'b0`), and the input gates the reset.
                    {
                        std::set<NodeId> dataLegs;
                        bool gatingSelect = false;
                        for (auto ei : m_->in[n]) {
                            const Edge& e = m_->edges[ei];
                            if (e.type != EdgeType::Comb) continue;
                            if (e.ctrl || e.op == Op::MuxSel) {
                                if (e.from < m_->topInput.size() && m_->topInput[e.from] && !model::classifyResetNode(*m_, e.from).reset) gatingSelect = true;
                            } else {
                                dataLegs.insert(e.from);
                            }
                        }
                        if (gatingSelect && dataLegs.size() < 2) return false;
                    }
                    for (auto ei : m_->in[n]) {
                        const Edge& e = m_->edges[ei];
                        if (e.type != EdgeType::Comb) return false;
                        if (e.proc < m_->procs.size() && m_->procs[e.proc].kind == model::ProcKind::Latch) return false;
                        const bool sel = inSelect || e.ctrl || e.op == Op::MuxSel;
                        if (e.from < m_->topInput.size() && m_->topInput[e.from]) {
                            if (!sel && !model::classifyResetNode(*m_, e.from).reset) return false;
                            continue;
                        }
                        if (walked.insert(uint64_t{e.from} << 1 | (sel ? 1 : 0)).second) st.push_back({e.from, sel});
                    }
                }
                continue;
            }
            if (!rf || !rf->clockResolved) return false;
            const SyncInfo si = syncChain(ri);
            if (!si.recognised || si.depth < minStages_ || si.first == ~0u || m_->flops[si.first].resets.empty())
                return false;
            for (const auto& res : m_->flops[si.first].resets) work.emplace_back(res.root.node, hop + 1);
        }
        return true;
    };
    // A source register whose Q depends only on its single recognised asynchronous reset (async reset to binary v,
    // released D the opposite binary constant !v, with no non-constant Seq inputs) is a reset-propagation flag.
    const auto isResetPropagationFlag = [&](uint32_t si) -> bool {
        const Flop& sf = m_->flops[si];
        if (!sf.clockResolved || sf.blocking || sf.hasIff || sf.edge == 3 || m_->procs[sf.proc].bodyTiming ||
            !sf.otherDomains.empty() || m_->multiSequential.count(sf.q) || cones_->hasUncertainCalls(si))
            return false;
        if (sf.resets.size() != 1 || !sf.resetTransitionKnown || sf.resetValues.size() != 1) return false;
        if (!isRecognisedResetSource(sf.resets[0].root.node)) return false;
        const char asserted = sf.resetValues[0];
        if (asserted != '0' && asserted != '1') return false;
        const char released = sf.releasedSource == kNoNode ? sf.releasedValue : m_->constantValue(sf.releasedSource);
        if (released != (asserted == '0' ? '1' : '0')) return false;
        for (auto ei : m_->in[sf.q]) {
            const Edge& e = m_->edges[ei];
            if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
            if (e.ctrl || !e.exact || m_->constantValue(e.from) != released) return false;
        }
        return true;
    };
    // Whether destination flop `di` samples `srcQ` directly (or inverted) as its sole live data input.
    const auto samplesSoleSource = [&](uint32_t di, NodeId srcQ) -> bool {
        const Flop& d = m_->flops[di];
        bool sawData = false;
        for (auto ei : m_->in[d.q]) {
            const Edge& e = m_->edges[ei];
            if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
            if (e.ctrl) {
                if (std::find(d.syncResetGuards.begin(), d.syncResetGuards.end(), e.from) != d.syncResetGuards.end())
                    continue;
                return false;
            }
            if (!(e.exact || e.flip) || e.approximate) return false;
            if (copyRootOf(e.from) != srcQ) return false;
            sawData = true;
        }
        return sawData;
    };
    // Reset propagation into a reset generator: s is the final stage of a recognised reset synchroniser, every use of d is an
    // asynchronous reset pin, an exact or inverted copy of one, or an exported reset output, and d's next value with s at its forced
    // (asserted) value and every other operand free equals the level at which d's reset is asserted.
    std::unordered_map<uint32_t, int> generatorLevels;   // 0 / 1: the asserted level of the reset d drives; -1: d is not only a reset
    const auto generatorLevel = [&](uint32_t di) -> int {
        const auto [slot, fresh] = generatorLevels.try_emplace(di, -1);
        if (!fresh) return slot->second;
        const Flop& d = m_->flops[di];
        std::set<int> levels;
        bool pins = false, exported = false, bad = false;
        std::vector<NodeId> nodes{d.q};
        std::set<NodeId> seen{d.q};
        for (size_t i = 0; i < nodes.size() && !bad; ++i) {
            const NodeId n = nodes[i];
            if (n < m_->topOutput.size() && m_->topOutput[n]) {
                // an exported name that is a clean reset name states its polarity; any other name leaves it to the register's own reset value
                const std::string full = stripBit(m_->nameOf(n));
                const model::ResetName nm = model::classifyResetName(full.substr(full.rfind('.') == std::string::npos ? 0 : full.rfind('.') + 1));
                if (nm.verdict == model::ResetName::Verdict::Data) { bad = true; break; }
                if (nm.verdict == model::ResetName::Verdict::Known) levels.insert(nm.activeLow ? 0 : 1);
                exported = true;
            }
            if (n >= m_->out.size()) continue;
            for (auto ei : m_->out[n]) {
                const Edge& e = m_->edges[ei];
                if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                if (e.type == EdgeType::AsyncCtl) pins = true;
                else if (e.type == EdgeType::Comb && !e.ctrl && (e.exact || e.flip)) { if (seen.insert(e.to).second) nodes.push_back(e.to); }
                else { bad = true; break; }
            }
        }
        // The asserted level is the value the register holds while its own reset is asserted. A pin use must assert at that level, and
        // an exported name that carries a polarity must agree with it.
        if (pins) {
            const auto it = assertedUses_.find(di);
            if (it == assertedUses_.end() || it->second.empty()) bad = true;
            else for (char c : it->second) levels.insert(c - '0');
        }
        if (!bad && (pins || exported) && d.resetValues.size() == 1 && (d.resetValues[0] == '0' || d.resetValues[0] == '1')) {
            const int own = d.resetValues[0] - '0';
            if (levels.empty() || (levels.size() == 1 && *levels.begin() == own)) slot->second = own;
        }
        return slot->second;
    };
    std::unordered_map<uint32_t, bool> finalSyncStages;
    const auto isFinalSyncStage = [&](uint32_t si) {
        const auto [slot, fresh] = finalSyncStages.try_emplace(si, false);
        if (fresh) {
            const SyncInfo x = syncChain(si);
            slot->second = x.recognised && x.depth >= minStages_ && !x.stages.empty() && x.stages.front() == si;
        }
        return slot->second;
    };
    std::unordered_map<uint64_t, bool> propagationMemo;
    const auto propagatesReset = [&](uint32_t si, uint32_t di) -> bool {
        // the cheap per-register filters come first: only a final synchroniser stage feeding a reset generator is memoised per pair
        const Flop& s = m_->flops[si];
        if (!isFinalSyncStage(si) || s.resetValues.size() != 1 || (s.resetValues[0] != '0' && s.resetValues[0] != '1')) return false;
        const int level = generatorLevel(di);
        if (level < 0) return false;
        const auto [slot, fresh] = propagationMemo.try_emplace(uint64_t{si} << 32 | di, false);
        if (!fresh) return slot->second;
        if (!evaluator_) evaluator_ = std::make_unique<ReleaseEvaluator>(*m_);
        const ReleaseEvaluator::Lookup lookup = [&](NodeId n) -> Tri {
            int flips = 0;
            NodeId cur = n;
            for (int hop = 0; hop < 64; ++hop) {
                if (cur == s.q) return Tri{static_cast<uint8_t>((s.resetValues[0] - '0') ^ flips), false};
                if (cur >= m_->in.size() || m_->in[cur].size() != 1) break;
                const Edge& e = m_->edges[m_->in[cur][0]];
                if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) break;
                if (e.flip && !e.exact) flips ^= 1;
                cur = e.from;
            }
            const char c = m_->constantValue(n);
            if (c == '0' || c == '1') return Tri{static_cast<uint8_t>(c - '0'), false};
            return Tri{2, false};
        };
        bool ok = false;
        const Tri v = evaluator_->nextValue(di, lookup, ok);
        slot->second = ok && v.known() && v.v == static_cast<uint8_t>(level);
        return slot->second;
    };
    struct Propagation { uint32_t destination = 0; uint32_t sourceSet = 0; std::map<std::string, uint32_t> sources; };
    std::map<std::string, Propagation> propagations;
    // Each destination's own reset set; destinations are walked in batches of one reset set, which decides what is pruned.
    std::vector<uint32_t> ownOf(m_->flops.size(), 0), order;
    for (uint32_t di = 0; di < m_->flops.size(); di++) {
        if (!m_->flops[di].clockResolved) continue;
        if (hasTiedActiveReset(m_->flops[di])) continue;   // held permanently in reset: never samples source data
        ownOf[di] = resetSetOf[di] != SourceClasses::kIgnore ? resetSetOf[di] : resetSetId(asyncSourcesWithPolarity(m_->flops[di]));
        order.push_back(di);
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return ownOf[a] < ownOf[b]; });
    // Mismatches are merged as the batches find them. One cone view per destination, in register order and listing sources by
    // Q, would keep the first destination of each key and the first source of each name: the lowest (destination, Q) is kept.
    {
        BatchWalker walker(*m_);
        std::vector<NodeId> qs;
        // Per batch: the mismatch each (destination, source reset set, launch, capture, edge, relation) maps to.
        std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, int, int>, ResetMismatch*> keyed;
        for (size_t first = 0; first < order.size();) {
            const uint32_t own = ownOf[order[first]];
            size_t last = first;
            while (last < order.size() && last - first < BatchWalker::kLanes && ownOf[order[last]] == own) ++last;
            qs.clear();
            for (size_t i = first; i < last; ++i) qs.push_back(m_->flops[order[i]].q);
            // A node whose reachable reset classes are all covered by the batch's reset set leads to no mismatch.
            walker.run(qs, [&](NodeId n) {
                const uint32_t set = resetClasses.setOf(n);
                return set != SourceClasses::kWide && classCovered(set, own);
            });
            keyed.clear();
            for (const NodeId q : walker.leaves()) {
                const uint32_t si = cones_->flopIndexOf(q);
                if (si == UINT32_MAX) continue;   // a top input
                const uint32_t rsId = resetSetOf[si];
                if (rsId == SourceClasses::kIgnore || covers(own, rsId)) continue;
                const Flop& sf = m_->flops[si];
                const auto& lanes = walker.lanes(q);
                for (size_t w = 0; w < BatchWalker::kWords; ++w)
                    for (uint64_t b = lanes[w]; b; b &= b - 1) {
                        const uint32_t di = order[first + w * 64 + static_cast<size_t>(std::countr_zero(b))];
                        if (di == si) continue;   // a register is not a source of its own cone
                        const Flop& d = m_->flops[di];
                        const auto pair = worstDomains(sf, d, *rel_);
                        const bool sameClock = pair.launch == pair.capture;
                        if (!sameClock && pair.relation == Relation::Same) continue;
                        if (!sameClock) {
                            const int32_t lastStage = dataSyncLastStage(di);
                            if (lastStage >= 0 && (consumersResetBySource(static_cast<uint32_t>(lastStage), sf, d) ||
                                                   (isResetPropagationFlag(si) && samplesSoleSource(di, sf.q))))
                                continue;
                        }
                        if (sameClock && propagatesReset(si, di)) {
                            const std::string key = "RDC-004:" + strippedName(di) + ">" + resetSetNames(rsId) + ">" + resetSetNames(own);
                            auto& p = propagations[key];
                            if (p.sources.empty() || di < p.destination) { p.destination = di; p.sourceSet = rsId; }
                            p.sources.emplace(strippedName(si), si);
                            continue;
                        }
                        auto [slot, fresh] = keyed.try_emplace({di, rsId, pair.launch, pair.capture, sf.edge, static_cast<int>(pair.relation)}, nullptr);
                        if (fresh) {
                            const char* rule = sameClock ? "RDC-004" : "RDC-005";
                            const std::string names = ">" + resetSetNames(rsId) + ">" + resetSetNames(own);
                            auto key = std::string(rule) + ":" + strippedName(di) + names + ":" + m_->domains[pair.launch].name + ":" +
                                       std::to_string(sf.edge) + ">" + m_->domains[pair.capture].name + ":" + std::to_string(d.edge) + ":" + relName(pair.relation);
                            auto& mismatch = mismatches[key];
                            if (di < mismatch.firstDestination) {
                                mismatch.firstDestination = di;
                                mismatch.dst = &d;
                                mismatch.sourceResets = *resetSets[rsId];
                                mismatch.destinationResets = asyncSourcesWithPolarity(d);
                                mismatch.relation = pair.relation;
                                mismatch.launch = pair.launch;
                                mismatch.capture = pair.capture;
                                mismatch.launchEdge = sf.edge;
                            }
                            slot->second = &mismatch;
                        }
                        const std::pair<uint32_t, NodeId> rank{di, q};
                        auto [source, added] = slot->second->firstSource.try_emplace(strippedName(si), rank, &sf);
                        if (!added && rank < source->second.first) source->second = {rank, &sf};
                    }
            }
            first = last;
        }
    }
    for (auto& [key, mismatch] : mismatches) {
        for (const auto& [name, ranked] : mismatch.firstSource) mismatch.sources.emplace(name, ranked.second);
        mismatch.firstSource.clear();
    }
    for (auto& [key, mismatch] : mismatches) {
        const bool sameClock = mismatch.launch == mismatch.capture;
        const char* rule = sameClock ? "RDC-004" : "RDC-005";
        evaluated[rule] += mismatch.sources.size();
        bool sameLeafOppositePolarity = false;
        for (const auto& [sn, sl] : mismatch.sourceResets)
            if (mismatch.destinationResets.count({sn, !sl})) { sameLeafOppositePolarity = true; break; }
        std::string rsn, rdn;
        for (const auto& [n, low] : mismatch.sourceResets)
            rsn += (rsn.empty() ? "" : ", ") + resetBitName(n) + (sameLeafOppositePolarity ? (low ? " (active-low)" : " (active-high)") : "");
        for (const auto& [n, low] : mismatch.destinationResets)
            rdn += (rdn.empty() ? "" : ", ") + resetBitName(n) + (sameLeafOppositePolarity ? (low ? " (active-low)" : " (active-high)") : "");
        const std::string dst = stripBit(m_->nameOf(mismatch.dst->q));
        const std::string first = mismatch.sources.begin()->first;
        const std::string sampled = mismatch.sources.size() == 1 ? first : std::to_string(mismatch.sources.size()) + " registers including " + first;
        const std::string dstResetText = !rdn.empty()
            ? "is reset by [" + rdn + "]"
            : hasTiedInactiveReset(*mismatch.dst) ? "its reset is tied inactive" : "is not reset";
        Diagnostic x = mk(rule, Severity::Warning,
                          "register " + dst + " samples " + sampled + (mismatch.sources.size() == 1 ? " which is" : " which are") + " asynchronously reset by [" + rsn + "], but " +
                          dstResetText + (sameClock ? " (same clock)" : " (different clocks: " + std::string(relName(mismatch.relation)) + ")") +
                          (sameClock ? ": when that reset asserts the source changes asynchronously to the destination clock" :
                                       ": asserting the source reset can change the sampled value without resetting the destination"),
                          procLoc(*mismatch.dst).loc, procLoc(*mismatch.dst).instancePath, Confidence::Heuristic);
        x.identityKey = key;
        x.signals = {dst};
        for (const auto& [name, source] : mismatch.sources) {
            x.signals.push_back(name);
            x.groupedSources.push_back(name);
            x.evidence.push_back({"source_register", name, procLoc(*source).loc});
        }
        if (ctx.config.resetContracts.present) {
            std::set<NodeId> srcNodes, dstNodes;
            for (const auto& [n, low] : mismatch.sourceResets) srcNodes.insert(n);
            for (const auto& [n, low] : mismatch.destinationResets) dstNodes.insert(n);
            citeSiteClauses(x, {&srcNodes, &dstNodes});
        }
        x.proofState = ProofState::NotAttempted;
        if (!sameClock) {
            x.message += "; reset compatibility is an unproved functional obligation, even with a data synchroniser";
            x.evidence.push_back({"obligation", "The destination must tolerate the reset-induced value change; metastability synchronisation alone does not establish this.", {}});
        }
        x.nextStep = "Reset both registers by the same reset, or ensure the destination value is don't-care while the source is in reset (documented obligation).";
        add(std::move(x));
    }
    for (const auto& [key, p] : propagations) {
        evaluated["RDC-004"] += p.sources.size();
        const Flop& dst = m_->flops[p.destination];
        const std::string dname = stripBit(m_->nameOf(dst.q));
        const std::string first = p.sources.begin()->first;
        const std::string sampled = p.sources.size() == 1 ? first : std::to_string(p.sources.size()) + " registers including " + first;
        Diagnostic x = mk("RDC-004", Severity::Info,
                          "register " + dname + " samples " + sampled + ", the final stage of a reset synchroniser asynchronously reset by [" + resetSetNames(p.sourceSet) +
                          "]: " + dname + " only drives resets, and its next value is its asserted level while that reset is asserted, so the reset propagates through it",
                          procLoc(dst).loc, procLoc(dst).instancePath, Confidence::Heuristic);
        x.identityKey = key;
        x.signals = {dname};
        for (const auto& [name, si] : p.sources) {
            x.signals.push_back(name);
            x.evidence.push_back({"source_register", name, procLoc(m_->flops[si]).loc});
        }
        x.evidence.push_back({"trust_model", "the generated reset is assumed to be used only as a reset, so its changes do not reach functional data", {}});
        x.proofState = ProofState::NotAttempted;
        add(std::move(x));
    }
    std::set<std::string> done;

    // Clock-domain nets (non-flop domain roots, clock signals, and their exact non-flop copies):
    // RDC-007's logic walk stops at clock nets so reading a clock net itself (`!clk_d & rst_n`) does not
    // walk back through the clock tree to its upstream divider/mux/enable register. Direct reads of a
    // divider, clock-gate enable, or clock-mux leg register (bypassing the clock net) are NOT excluded.
    std::unordered_set<NodeId> clockNetNodes;
    {
        std::vector<NodeId> work;
        auto seedClockNet = [&](NodeId n) {
            if (n != kNoNode && !m_->flopOfQ.count(n) && clockNetNodes.insert(n).second)
                work.push_back(n);
        };
        for (const auto& dm : m_->domains) seedClockNet(dm.root);
        for (const auto& f : m_->flops) {
            seedClockNet(f.clockSignal);
            for (NodeId oc : f.otherClockSignals) seedClockNet(oc);
        }
        while (!work.empty()) {
            const NodeId cn = work.back();
            work.pop_back();
            if (cn < m_->out.size()) {
                for (auto ei : m_->out[cn]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && e.exact && !e.ctrl && !m_->isDeadEdge(e) && !m_->isConstantProc(e.proc))
                        seedClockNet(e.to);
                }
            }
            if (cn < m_->in.size()) {
                for (auto ei : m_->in[cn]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && e.exact && !e.ctrl && !m_->isDeadEdge(e) && !m_->isConstantProc(e.proc))
                        seedClockNet(e.from);
                }
            }
        }
    }

    // --- reset trees built from logic: RDC-008 (combined resets), RDC-009 (gated/muxed), RDC-014 (data flop), RDC-017 (select from another reset domain)
    std::set<NodeId> doneRoots;

    // --- RDC-007 helpers: one finding per (leaf register, receiving domain) and test-mode legs.
    // The logic walk of a reset root stops at a clock net or a top input and passes through intermediate reset roots. A leaf that the
    // root reaches only through intermediate roots is reported at the nearest root whose users already cover the receiving domain.
    struct RootReach {
        std::vector<NodeId> leaves;   // register leaves reached without passing another root
        std::vector<NodeId> inter;    // the nearest intermediate roots
        bool logic = false;           // a non-exact edge in this region: the root is reported by the logic-built-reset analysis
    };
    std::unordered_map<NodeId, RootReach> reachMemo;
    const auto reachOf = [&](NodeId rt) -> const RootReach& {
        auto [it, fresh] = reachMemo.try_emplace(rt);
        if (!fresh) return it->second;
        RootReach& out = it->second;
        std::unordered_set<NodeId> seenR{rt};
        std::vector<NodeId> stR{rt};
        while (!stR.empty()) {
            const NodeId n = stR.back();
            stR.pop_back();
            if (n != rt && clockNetNodes.count(n)) continue;
            if (m_->flopOfQ.count(n)) { out.leaves.push_back(n); continue; }
            if (n != rt && n < m_->topInput.size() && m_->topInput[n]) continue;
            if (n != rt && resetRoots.count(n)) { out.inter.push_back(n); continue; }
            if (n >= m_->in.size()) continue;
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                if (!e.exact) out.logic = true;
                if (seenR.insert(e.from).second) stR.push_back(e.from);
            }
        }
        return out;
    };
    // The worst differing user domain of root `rt` for register leaf `leaf` (Async > Unknown > Exclusive), as the finding names it.
    const auto worstDomainOf = [&](NodeId rt, NodeId leaf) -> std::optional<uint32_t> {
        const auto fit = m_->flopOfQ.find(leaf);
        const auto uit = usersOf.find(rt);
        if (fit == m_->flopOfQ.end() || uit == usersOf.end() || !m_->flops[fit->second].clockResolved) return std::nullopt;
        const Flop& sf = m_->flops[fit->second];
        const auto rank = [](Relation r) { return r == Relation::Async ? 3 : r == Relation::Unknown ? 2 : r == Relation::Exclusive ? 1 : 0; };
        std::optional<uint32_t> best;
        int bestRank = 0;
        for (auto ui : uit->second) {
            const Flop& uf = m_->flops[ui];
            if (!uf.clockResolved || isRdc007Exempt(rt, ui)) continue;
            const int r = rank(rel_->relation(sf.domain, uf.domain));
            if (r > bestRank) { bestRank = r; best = uf.domain; }
        }
        return best;
    };
    // The receiving domains that root `rt`, or a nearer root it passes through, actually reports for leaf `leaf`: {reached, domains}.
    // Memoised per (root, leaf). A root covers only the domain its own finding names.
    std::unordered_map<uint64_t, std::pair<bool, std::set<uint32_t>>> coverMemo;
    std::function<const std::pair<bool, std::set<uint32_t>>&(NodeId, NodeId)> coverOf = [&](NodeId rt, NodeId leaf) -> const std::pair<bool, std::set<uint32_t>>& {
        const uint64_t key = uint64_t{rt} << 32 | leaf;
        if (const auto it = coverMemo.find(key); it != coverMemo.end()) return it->second;
        std::pair<bool, std::set<uint32_t>> result;
        const RootReach& reach = reachOf(rt);
        const bool direct = std::find(reach.leaves.begin(), reach.leaves.end(), leaf) != reach.leaves.end();
        if (direct) result.first = true;
        for (const NodeId inner : reach.inter) {
            const auto& sub = coverOf(inner, leaf);
            if (sub.first) { result.first = true; result.second.insert(sub.second.begin(), sub.second.end()); }
        }
        if (result.first && reach.logic) {
            if (const auto w = worstDomainOf(rt, leaf); w && (direct || !result.second.count(*w))) result.second.insert(*w);
        }
        return coverMemo.emplace(key, std::move(result)).first->second;
    };
    for (auto& [root, users] : usersOf) {
        const Flop& u0 = m_->flops[users[0]];
        std::string rname = stripBit(m_->nameOf(root));
        uint32_t gi = 0;
        const Flop* gen = flopBehind(root, &gi);
        if (gen) {
            // a register drives the reset: a recognised synchroniser is RDC-003/006 territory, a data register is a pulse-width risk
            const SyncInfo genChain = syncChain(gi);
            if (genChain.recognised) continue;
            evaluated["RDC-014"]++;
            // A chain past the recognition limit may well be a synchroniser: it is not classified as a data register.
            const bool beyondLimit = genChain.why == "chain longer than the analysis limit";
            Diagnostic d = beyondLimit
                ? mk("RDC-014", Severity::Warning, "asynchronous reset " + rname + " is driven by register " + flopName(*gen) + " whose chain exceeds the analysis limit: "
                     "it is neither recognised as a reset synchroniser nor classed as a data register, and its pulse width is unproved",
                     procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved)
                : mk("RDC-014", Severity::Warning, "asynchronous reset " + rname + " is driven by data register " + flopName(*gen) +
                     " (not a reset synchroniser): its pulse width is not established by the RTL and the reset can assert spuriously",
                     procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            if (beyondLimit) ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' is driven by a chain beyond the analysis limit");
            d.signals = {rname};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"proof_obligation", "minimum reset pulse width and glitch freedom depend on timing/library data and are not analysed", {}});
            add(std::move(d));
            continue;
        }
        if (usersOf.at(root).empty() || m_->nodes[root].sym == nullptr) continue;
        if (!m_->in[root].size()) continue;   // primary input or undriven: nothing to analyse
        std::vector<NodeId> resetLeaves, dataLeaves, undecidedLeaves, unknownLeaves;
        std::unordered_set<NodeId> seen{root};
        std::vector<NodeId> st{root};
        bool logicSeen = false;
        bool coneUnknown = false;   // an X or Z literal in the fan-in of the selected bit
        // The shape of the whole fan-in tree, not only the edges entering R: a wire between combinations of different kinds hides
        // a mixed tree, and an inverted, approximate or unclassified operator anywhere leaves the combination unclassified.
        std::set<Op> treeOps;
        bool treeUnclassified = false;
        while (!st.empty()) {
            NodeId n = st.back(); st.pop_back();
            if (n != root && (resetRoots.count(n) || (n < m_->topInput.size() && m_->topInput[n]))) {
                // A leaf is a reset (a reset root, or a primary input the shared classifier takes as one), data, or undecided: a name
                // that may or may not be a reset is not a claim either way.
                const model::ResetClass cls = resetRoots.count(n) ? model::ResetClass{} : model::classifyResetNode(*m_, n);
                const bool isReset = resetRoots.count(n) || cls.reset;
                (isReset ? resetLeaves : cls.undecided ? undecidedLeaves : dataLeaves).push_back(n);
                continue;
            }
            if (m_->flopOfQ.count(n)) {
                uint32_t fi = m_->flopOfQ.at(n);
                (syncChain(fi).recognised ? resetLeaves : dataLeaves).push_back(n);
                continue;
            }
            // a driver cycle (`a = b; b = a;`): what the loop carries is not known
            if (n != root && m_->traceRoot(n).kind == RootKind::Cycle) { unknownLeaves.push_back(n); continue; }
            // an X or Z literal in the driver of this bit (`n = rst_n & 1'bx`) makes the combination unknown, whatever else it reads
            if (n != root && m_->nodes[n].sym && unknownLiteralAtBit(m_->nodes[n].sym, m_->nodes[n].bit, m_->collapsed(n))) coneUnknown = true;
            if (n != root && m_->in[n].empty()) {
                // No driver in the model (a black box output, an undriven net or an unknown value): what it contributes is not known.
                // A binary constant is neither a reset nor data.
                const char value = m_->constantValue(n);
                if (value != '0' && value != '1') unknownLeaves.push_back(n);
                continue;
            }
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                if (!e.exact) {
                    logicSeen = true;
                    if ((e.op == Op::And || e.op == Op::Or) && e.inv == 0 && !e.ctrl && !e.approximate) treeOps.insert(e.op);
                    else treeUnclassified = true;
                } else if (n == root) {
                    treeUnclassified = true;   // a pure copy of the combination is not one of the classified forms
                }
                if (seen.insert(e.from).second) st.push_back(e.from);
            }
        }
        if (!logicSeen || !doneRoots.insert(root).second) continue;
        // RDC-007 for resets built by logic: walk backward from `root` through Comb edges, stopping at
        // clock nets (and their exact non-flop copies) and reset roots/top inputs, to find register leaves;
        // pick the worst differing user domain (Async > Unknown > Exclusive) per leaf register.
        {
            std::vector<NodeId> rdc007FlopLeaves;
            std::unordered_set<NodeId> seen7{root};
            std::vector<NodeId> st7{root};
            while (!st7.empty()) {
                const NodeId n = st7.back();
                st7.pop_back();
                if (n != root && clockNetNodes.count(n)) continue;
                if (m_->flopOfQ.count(n)) {
                    rdc007FlopLeaves.push_back(n);
                    continue;
                }
                if (n != root && n < m_->topInput.size() && m_->topInput[n]) continue;
                if (n >= m_->in.size()) continue;
                for (auto ei : m_->in[n]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && seen7.insert(e.from).second) st7.push_back(e.from);
                }
            }
            for (auto n : rdc007FlopLeaves) {
                auto fit = m_->flopOfQ.find(n);
                if (fit == m_->flopOfQ.end()) continue;
                const Flop& sf = m_->flops[fit->second];
                if (!sf.clockResolved) continue;
                auto rankOf = [](Relation r) { return r == Relation::Async ? 3 : r == Relation::Unknown ? 2 : r == Relation::Exclusive ? 1 : 0; };
                const Flop* worstUf = nullptr;
                Relation worstRel = Relation::Same;
                for (auto ui : users) {
                    const Flop& uf = m_->flops[ui];
                    if (!uf.clockResolved || isRdc007Exempt(root, ui)) continue;
                    Relation rel = rel_->relation(sf.domain, uf.domain);
                    if (rankOf(rel) > rankOf(worstRel)) { worstRel = rel; worstUf = &uf; }
                }
                if (!worstUf) continue;
                // one finding per (leaf, receiving domain): a leaf reached only through nearer roots that already cover this domain
                // is reported there
                {
                    const RootReach& rr = reachOf(root);
                    if (std::find(rr.leaves.begin(), rr.leaves.end(), n) == rr.leaves.end()) {
                        bool covered = false;
                        for (const NodeId inner : rr.inter) {
                            const auto& c = coverOf(inner, n);
                            covered = covered || (c.first && c.second.count(worstUf->domain));
                        }
                        if (covered) continue;
                    }
                }
                evaluated["RDC-007"]++;
                Diagnostic x = mk("RDC-007", worstRel == Relation::Async ? Severity::Error : worstRel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                                  "reset " + rname + " is built by logic from register " + stripBit(m_->nameOf(n)) + " in clock domain '" + m_->domains[sf.domain].name +
                                  "' but resets registers in domain '" + m_->domains[worstUf->domain].name + "' [clock relationship " + relName(worstRel) + "]: its release is asynchronous to the receiving clock",
                                  procLoc(*worstUf).loc, procLoc(*worstUf).instancePath, worstRel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                x.signals = {rname, stripBit(m_->nameOf(n))};
                x.identityKey = m_->domains[sf.domain].name + ":" + std::to_string(sf.edge) + ">" + m_->domains[worstUf->domain].name + ":" + std::to_string(worstUf->edge) + ":" + relName(worstRel);
                add(std::move(x));
                if (worstRel == Relation::Unknown) ctx.report.markIncomplete("clock-relationships", "reset '" + rname + "' crosses unrelated clocks");
            }
        }
        // Leaves with no modelled driver are undecided as well: their role as reset or data is not known.
        std::vector<NodeId> undecided = undecidedLeaves;
        undecided.insert(undecided.end(), unknownLeaves.begin(), unknownLeaves.end());
        auto leafName = [&](NodeId n) {
            const std::string raw = m_->nameOf(n);
            const std::string base = stripBit(raw);
            const auto* sym = m_->nodes[n].sym;
            if (raw == base || !sym || !sym->getType().isIntegral()) return raw;
            const auto range = sym->getType().getFixedRange();
            const int64_t offset = static_cast<int64_t>(m_->nodes[n].bit);
            const int64_t index = range.left > range.right ? range.right + offset : range.right - offset;
            return base + "[" + std::to_string(index) + "]";
        };
        const auto names = [&](const std::vector<NodeId>& nodes) {
            std::string out;
            std::set<std::string> seenNames;
            for (auto n : nodes) {
                std::string nm = leafName(n);
                if (seenNames.insert(nm).second) out += (out.empty() ? "" : ", ") + nm;
            }
            return out;
        };
        // An X or Z literal in the driver expression is an operand with no modelled value, like an undriven net.
        const bool literalUnknown = coneUnknown || unknownLiteralAtBit(m_->nodes[root].sym, m_->nodes[root].bit, m_->collapsed(root));
        const bool incomplete = !undecided.empty() || literalUnknown;
        // Why the fan-in of this reset is not decided by its known sources alone (RDC-008 / RDC-009).
        std::string incompleteWhy;
        if (!undecidedLeaves.empty())
            incompleteWhy = "whether [" + names(undecidedLeaves) + "] is a reset is undecided (not clearly named like one, no asynchronous reset use)";
        if (!unknownLeaves.empty())
            incompleteWhy += (incompleteWhy.empty() ? "" : "; ") + std::string("[") + names(unknownLeaves) + "] has no modelled driver (black box, undriven, cyclic or unknown value), so its role is undecided";
        if (literalUnknown)
            incompleteWhy += (incompleteWhy.empty() ? "" : "; ") + std::string("its fan-in has an X or Z literal operand, so its value is not decided by the reset inputs alone");
        if (incomplete && dataLeaves.empty()) {
            // Whether a source is a reset decides between a combination of resets (RDC-008) and a reset gated by functional signals
            // (RDC-009): neither is claimed.
            const std::string l = names(resetLeaves);
            const std::string& why = incompleteWhy;
            ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' combines a source with no modelled driver, an unknown value or an undecided role");
            const size_t candidates = resetLeaves.size() + undecided.size() + (literalUnknown ? 1 : 0);
            for (const char* id : {"RDC-008", "RDC-009"}) {
                if (std::string(id) == "RDC-008" && candidates < 2) continue;
                evaluated[id]++;
                Diagnostic d = mk(id, Severity::Warning, std::string("Unresolved: ") + (std::string(id) == "RDC-008" ? "reset " + rname + " may be a logic combination of resets" +
                                  (l.empty() ? "" : " [" + l + "]") : "reset " + rname + " may be gated or multiplexed by functional signals") + ": " + why,
                                  procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
                d.signals = {rname};
                d.nextStep = "Name the source like a reset, or use it as an asynchronous reset.";
                add(std::move(d));
            }
        } else if (resetLeaves.size() >= 2 && dataLeaves.empty()) {
            evaluated["RDC-008"]++;
            const std::string l = names(resetLeaves);
            // Polarity: the receivers reset on R with one polarity, and each source has that polarity when its own polarity is known.
            // Contradictory polarities leave the assertion condition unestablished (Unresolved), never one chosen polarity.
            std::set<bool> userLow;
            for (auto ui : users)
                for (auto& r : m_->flops[ui].resets)
                    if (m_->aliasRoot(r.signal) == root) userLow.insert(r.activeLow);
            const bool polKnown = !userLow.empty();
            const bool rootLow = polKnown && *userLow.begin();
            bool contradictory = userLow.size() > 1;
            for (auto n : resetLeaves) {
                int leafLow = -1;   // -1 unknown, 0 active-high, 1 active-low
                if (resetRoots.count(n)) {
                    std::set<bool> lows;
                    for (auto ui : usersOf.at(n))
                        for (auto& r : m_->flops[ui].resets)
                            if (r.root.node == n) lows.insert(r.activeLow);
                    if (lows.size() > 1) contradictory = true;
                    else if (lows.size() == 1) leafLow = *lows.begin();
                } else if (auto fit = m_->flopOfQ.find(n); fit != m_->flopOfQ.end()) {
                    const Flop& sf = m_->flops[fit->second];
                    if (sf.resetValues.size() == 1 && (sf.resetValues[0] == '0' || sf.resetValues[0] == '1')) leafLow = sf.resetValues[0] == '0';
                } else if (n < m_->topInput.size() && m_->topInput[n]) {
                    const model::ResetClass cls = model::classifyResetNode(*m_, n);
                    if (cls.polarityKnown) leafLow = cls.activeLow;
                }
                if (leafLow >= 0 && polKnown && (leafLow == 1) != rootLow) contradictory = true;
            }
            const bool classified = polKnown && !contradictory && !treeUnclassified && treeOps.size() == 1;
            const bool assertsOnAny = classified && ((rootLow && *treeOps.begin() == Op::And) || (!rootLow && *treeOps.begin() == Op::Or));
            std::string how = contradictory ? "its sources or receivers have contradictory polarities, so the assertion condition is not established"
                            : !classified ? "the operators combining them are not classified (not a plain homogeneous AND/OR of true-polarity sources)"
                            : assertsOnAny ? "it asserts when any source asserts and releases only after the last releases (reconvergent release)"
                                           : "it asserts only while all sources assert and releases as soon as any releases (conditional reset: a partial assertion does not reset)";
            Diagnostic d = mk("RDC-008", Severity::Warning, "reset " + rname + " is a logic combination of " + std::to_string(resetLeaves.size()) + " resets [" + l + "]: " + how,
                              procLoc(u0).loc, procLoc(u0).instancePath, classified ? Confidence::Heuristic : Confidence::Unresolved);
            d.signals = {rname};
            d.evidence.push_back({"combination", contradictory ? "contradictory-polarity" : !classified ? "unclassified" : assertsOnAny ? "assert-on-any" : "assert-on-all", {}});
            if (classified) d.evidence.push_back({"assumption", std::string("sources have the same polarity as ") + rname + " (" + (rootLow ? "active low" : "active high") + ")", {}});
            add(std::move(d));
        } else if (!dataLeaves.empty()) {
            evaluated["RDC-009"]++;
            const std::string l = names(dataLeaves);
            Diagnostic d = mk("RDC-009", Severity::Warning, "reset " + rname + " is gated or multiplexed by functional signal(s) [" + l + "]: combinational reset logic can glitch and assert the reset spuriously",
                              procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            d.signals = {rname};
            d.nextStep = "Register the gated reset in a flop clocked by the receiving domain, or use a dedicated reset controller.";
            add(std::move(d));
            if (incomplete) {
                // The known gate stays visible; its other inputs are not all decided, which is said separately.
                evaluated["RDC-009"]++;
                Diagnostic u = mk("RDC-009", Severity::Warning, "Unresolved: reset " + rname + " is gated by functional signal(s) [" + l + "] and its fan-in is incomplete: " + incompleteWhy,
                                  procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
                u.signals = {rname};
                add(std::move(u));
                ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' has an incomplete fan-in beside its functional gate");
            }
            for (auto n : dataLeaves) {
                auto fit = m_->flopOfQ.find(n);
                if (fit == m_->flopOfQ.end()) continue;
                const Flop& sf = m_->flops[fit->second];
                std::set<NodeId> rs = asyncSources(sf);
                // The receivers are the other registers the reset controls: the select's own use of the reset is compared with them,
                // not counted as one of them (a select that resets itself through the net it gates would otherwise match itself).
                // Each receiver is compared on its own: a union of the receivers' sources would hide a source one receiver lacks.
                std::vector<uint32_t> mismatched;   // receivers whose reset sources differ from the select's
                const bool noReceiver = users.size() <= 1;   // the select is the only user: it is compared with no sources
                for (auto r : users) {
                    if (r == fit->second) continue;
                    auto a = asyncSources(m_->flops[r]);
                    if (a != rs) mismatched.push_back(r);
                }
                const bool clockUnknown = !sf.clockResolved || hasNoModelledDriver(*m_, sf.clockSignal);
                bool sourceUnknown = false;
                for (auto s : rs) sourceUnknown = sourceUnknown || hasNoModelledDriver(*m_, s);
                for (auto r : mismatched)
                    for (auto s : asyncSources(m_->flops[r])) sourceUnknown = sourceUnknown || hasNoModelledDriver(*m_, s);
                // A source with no modelled driver (black box, undriven, cyclic) can be the reason the sets agree or differ: the domain
                // comparison is then unresolved even when every set matches.
                if (!mismatched.empty() || (noReceiver && !rs.empty()) || clockUnknown || sourceUnknown) {
                    evaluated["RDC-017"]++;
                    // A select with no identified clock, or whose reset comes from a black box, undriven or cyclic net, cannot be placed
                    // in a domain: that is Unresolved, not a domain difference.
                    const bool unresolved = clockUnknown || sourceUnknown;
                    std::string receivers;
                    for (auto r : mismatched) receivers += (receivers.empty() ? "" : ", ") + stripBit(flopName(m_->flops[r]));
                    std::string why = clockUnknown ? "clocked by an unidentified clock"
                                    : (mismatched.empty() && sourceUnknown) ? "reset by a source that is not decided, so its reset domain is not placed"
                                    : "in a different reset domain than the registers the reset controls";
                    if (!receivers.empty() && !clockUnknown) why += " [" + receivers + " " + (mismatched.size() == 1 ? "is" : "are") + " reset by different sources]";
                    if (!clockUnknown && sourceUnknown)
                        why += "; its reset or the reset's receivers have a source with no modelled driver (black box, undriven or cyclic), so the domain comparison is unresolved";
                    Diagnostic x = mk("RDC-017", Severity::Warning, "the select/gate " + stripBit(m_->nameOf(n)) + " of reset " + rname + " comes from a register that is " + why,
                                      procLoc(sf).loc, procLoc(sf).instancePath, unresolved ? Confidence::Unresolved : Confidence::Heuristic);
                    x.signals = {rname, stripBit(m_->nameOf(n))};
                    if (unresolved) ctx.report.markIncomplete("reset-extraction", "select/gate " + stripBit(m_->nameOf(n)) + " of reset " + rname + " has an unresolved reset domain or clock");
                    add(std::move(x));
                }
            }
        }
    }

    // --- RDC-030: synchronisation attributes on the stages of a recognised reset synchroniser (same policy as CDC-012)
    {
        std::set<uint32_t> checked;
        for (auto& [root, users] : usersOf) {
            uint32_t last = 0;
            if (!flopBehind(root, &last) || !checked.insert(last).second) continue;
            const SyncInfo si = syncChain(last);
            if (!si.recognised) continue;
            evaluated["RDC-030"]++;
            size_t marked = 0, unknown = 0;
            for (auto stage : si.stages) {
                const auto* symbol = m_->nodes[m_->flops[stage].q].sym;
                const StageMark mark = symbol ? stageMark(*ctx.design.compilation, *symbol) : StageMark::Absent;
                if (mark == StageMark::On) marked++;
                else if (mark == StageMark::Unknown) unknown++;
            }
            const bool partial = marked && marked < si.stages.size();
            if (!partial && !unknown && !(marked == 0 && ctx.config.profile == "fpga")) continue;
            const Flop& stage = m_->flops[last];
            // A marker whose value is X/Z/non-constant cannot establish or refute the marking: Unresolved, never suppressed.
            std::string text = unknown ? "reset synchroniser " + stripBit(flopName(stage)) + ": " + std::to_string(unknown) + " stage(s) carry an ASYNC_REG/sverity_sync "
                                         "attribute whose value is not a known 0 or 1, so their marking is unresolved"
                               : "reset synchroniser " + stripBit(flopName(stage)) + (partial ?
                                 ": only " + std::to_string(marked) + " of " + std::to_string(si.stages.size()) + " stages carry the ASYNC_REG/sverity_sync attribute" :
                                 " has no ASYNC_REG attribute on its stages (profile fpga): placement and timing tools may not keep the stages together");
            Diagnostic d = mk("RDC-030", Severity::Warning, text, procLoc(stage).loc, procLoc(stage).instancePath,
                              unknown ? Confidence::Unresolved : Confidence::Established);
            d.signals = {stripBit(m_->nameOf(root)), stripBit(flopName(stage))};
            d.nextStep = "Mark every stage of the reset synchroniser with (* ASYNC_REG = \"TRUE\" *).";
            if (unknown) ctx.report.markIncomplete("reset-extraction", "synchronisation attributes of reset synchroniser " + stripBit(flopName(stage)) + " are unresolved");
            add(std::move(d));
        }
    }

    // --- RDC-016: intermediate stages of a recognised reset synchroniser must not feed anything else (one finding per stage)
    // Follow exact non-control combinational copies (wires and instance port connections) to see through prim_flop boundaries.
    std::set<uint32_t> checkedLast;
    std::set<NodeId> reportedStages;
    for (auto& [root, users] : usersOf) {
        uint32_t gi = 0;
        if (!flopBehind(root, &gi)) continue;
        SyncInfo si = syncChain(gi);
        if (!si.recognised || !checkedLast.insert(gi).second) continue;
        for (size_t k = 1; k < si.stages.size(); k++) {
            const Flop& stage = m_->flops[si.stages[k]];
            if (reportedStages.count(stage.q)) continue;
            const Flop& next = m_->flops[si.stages[k - 1]];
            std::vector<NodeId> work{stage.q};
            std::unordered_set<NodeId> seen{stage.q};
            bool feedsOther = false;
            for (size_t wi = 0; wi < work.size() && !feedsOther; ++wi) {
                const NodeId n = work[wi];
                if ((n < m_->topOutput.size() && m_->topOutput[n]) || blackBoxNodes_.count(n) || clockDomainRoots_.count(n)) {
                    feedsOther = true;
                    break;
                }
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                    if (e.type == EdgeType::Comb && e.exact && !e.ctrl) {
                        if (seen.insert(e.to).second) work.push_back(e.to);
                        continue;
                    }
                    const bool toNext = e.type == EdgeType::Seq && e.to == next.q && !e.ctrl;
                    if (!toNext) {
                        feedsOther = true;
                        break;
                    }
                }
            }
            if (feedsOther) {
                reportedStages.insert(stage.q);
                evaluated["RDC-016"]++;
                Diagnostic d = mk("RDC-016", Severity::Warning, "intermediate stage " + flopName(stage) + " of the reset synchroniser for " + stripBit(m_->nameOf(root)) +
                                  " also feeds other logic: only the last stage may be used",
                                  procLoc(stage).loc, procLoc(stage).instancePath, Confidence::Established);
                add(std::move(d));
            }
        }
    }

    // --- RDC-019: a reset also used as functional data (SYNCASYNCNET-style mix)
    for (auto& [root, users] : usersOf) {
        const std::string rootName = stripBit(m_->nameOf(root));
        std::vector<std::string> funcUsers;
        if (!hasFunctionalUse(root, &funcUsers, /*exemptInputDelayDomain=*/true)) continue;
        evaluated["RDC-019"]++;
        const Flop& u0 = m_->flops[users[0]];
        Diagnostic d = mk("RDC-019", Severity::Warning, "reset " + rootName + " asynchronously resets " + std::to_string(users.size()) +
                          " register(s) and is also used as a functional signal (synchronous reset or data): the two uses have different timing requirements",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
        d.signals = {rootName};
        for (const auto& u : funcUsers) d.signals.push_back(u);
        add(std::move(d));
    }

    // An unreset bit that is itself an asynchronous control of another register, or a primary output (directly or through exact moves).
    const auto reachesControlOrOutput = [&](NodeId start) {
        std::vector<NodeId> work{start};
        std::unordered_set<NodeId> seen{start};
        while (!work.empty()) {
            const NodeId n = work.back();
            work.pop_back();
            if (n < m_->topOutput.size() && m_->topOutput[n]) return true;
            for (auto ei : m_->out[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::AsyncCtl) return true;
                if (e.type == EdgeType::Comb && e.exact && !e.ctrl && seen.insert(e.to).second) work.push_back(e.to);
            }
        }
        return false;
    };
    // --- RDC-020 / RDC-010: partially reset state
    std::map<std::string, std::vector<uint32_t>> bySym;
    for (uint32_t i = 0; i < m_->flops.size(); i++) bySym[stripBit(m_->nameOf(m_->flops[i].q))].push_back(i);
    for (auto& [name, idx] : bySym) {
        bool anyReset = false, anyPlain = false, anyUndecided = false;
        for (auto i : idx) {
            const bool undecided = undecidedBit(m_->flops[i]);
            anyUndecided = anyUndecided || undecided;
            anyReset = anyReset || (resetsBit(m_->flops[i]) && !undecided);
            anyPlain = anyPlain || isUnreset(m_->flops[i]);
        }
        if (anyUndecided && (anyReset || anyPlain)) {
            // some bits are reset or held and others have an undecided reset: the partial reset itself is not established
            evaluated["RDC-020"]++;
            Diagnostic d = mk("RDC-020", Severity::Warning, "Unresolved: register " + name + " has bits whose asynchronous reset value is not decided, so whether it is partly reset is not established",
                              procLoc(m_->flops[idx[0]]).loc, procLoc(m_->flops[idx[0]]).instancePath, Confidence::Unresolved);
            d.signals = {name};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
            continue;
        }
        if (!anyReset || !anyPlain) continue;
        evaluated["RDC-020"]++;
        bool feeds = false, controlsOrOutputs = false;
        for (auto i : idx) if (isUnreset(m_->flops[i])) {
            for (auto ei : m_->out[m_->flops[i].q]) { auto t = m_->edges[ei].type; feeds = feeds || t == EdgeType::Comb || t == EdgeType::Seq; }
            controlsOrOutputs = controlsOrOutputs || reachesControlOrOutput(m_->flops[i].q);
        }
        const bool warn = feeds || controlsOrOutputs;
        const Flop& f0 = m_->flops[idx[0]];
        Diagnostic d = mk("RDC-020", warn ? Severity::Warning : Severity::Info, "register " + name + " is only partly reset: some bits have a reset, others keep their value" +
                          (feeds ? " and feed other logic, so that logic sees a mix of reset and stale bits after reset"
                           : controlsOrOutputs ? " and drive an asynchronous control or a primary output, which see a mix of reset and stale bits after reset" : ""),
                          procLoc(f0).loc, procLoc(f0).instancePath, Confidence::Heuristic);
        d.signals = {name};
        add(std::move(d));
    }
    done.clear();
    // Only unreset sources of the destination's own instance are read.
    std::unordered_map<std::string, uint32_t> pathIds;
    const auto pathId = [&](const Flop& f) {
        return pathIds.try_emplace(procLoc(f).instancePath, static_cast<uint32_t>(pathIds.size())).first->second;
    };
    const auto unresetClasses = cones_->classify([&](uint32_t fi) {
        const Flop& f = m_->flops[fi];
        return (isUnreset(f) || undecidedBit(f)) ? pathId(f) : SourceClasses::kIgnore;
    }, false);
    // Batched by the destination's instance; per (source, destination) name pair the lowest (destination, source Q) is kept,
    // as one cone view per destination in register order, listing sources by Q, would find it first.
    {
        std::vector<uint32_t> order, ownOf(m_->flops.size(), 0);
        for (uint32_t di = 0; di < m_->flops.size(); di++)
            if (resetsBit(m_->flops[di])) { ownOf[di] = pathId(m_->flops[di]); order.push_back(di); }
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return ownOf[a] < ownOf[b]; });
        // Per (source name, destination name), as interned name ids: the lowest (destination, source Q, source index).
        std::unordered_map<std::string_view, uint32_t> nameIds;
        std::vector<uint32_t> nameIdOf(m_->flops.size(), UINT32_MAX);
        const auto nameId = [&](uint32_t fi) {
            if (nameIdOf[fi] == UINT32_MAX)
                nameIdOf[fi] = nameIds.try_emplace(strippedName(fi), static_cast<uint32_t>(nameIds.size())).first->second;
            return nameIdOf[fi];
        };
        std::unordered_map<uint64_t, std::tuple<uint32_t, NodeId, uint32_t>> firstPair;
        BatchWalker walker(*m_);
        std::vector<NodeId> qs;
        for (size_t first = 0; first < order.size();) {
            const uint32_t own = ownOf[order[first]];
            size_t last = first;
            while (last < order.size() && last - first < BatchWalker::kLanes && ownOf[order[last]] == own) ++last;
            qs.clear();
            for (size_t i = first; i < last; ++i) qs.push_back(m_->flops[order[i]].q);
            // Nothing behind a node whose unreset registers all sit in other instances is read.
            walker.run(qs, [&](NodeId n) {
                const uint32_t set = unresetClasses.setOf(n);
                if (set == SourceClasses::kWide) return false;
                const auto& classes = unresetClasses.classes(set);
                return !std::binary_search(classes.begin(), classes.end(), own);
            });
            for (const NodeId q : walker.leaves()) {
                const uint32_t si = cones_->flopIndexOf(q);
                if (si == UINT32_MAX) continue;   // a top input
                const Flop& sf = m_->flops[si];
                if (!isUnreset(sf) && !undecidedBit(sf)) continue;
                const auto& lanes = walker.lanes(q);
                for (size_t w = 0; w < BatchWalker::kWords; ++w)
                    for (uint64_t b = lanes[w]; b; b &= b - 1) {
                        const uint32_t di = order[first + w * 64 + static_cast<size_t>(std::countr_zero(b))];
                        if (di == si) continue;   // a register is not a source of its own cone
                        const Flop& d = m_->flops[di];
                        if (procLoc(sf).instancePath != procLoc(d).instancePath) continue;   // unreset register of the same module instance
                        const std::tuple<uint32_t, NodeId, uint32_t> rank{di, q, si};
                        auto [it, fresh] = firstPair.try_emplace(uint64_t{nameId(si)} << 32 | nameId(di), rank);
                        if (!fresh && rank < it->second) it->second = rank;
                    }
            }
            first = last;
        }
        std::vector<std::tuple<uint32_t, NodeId, uint32_t>> kept;
        kept.reserve(firstPair.size());
        for (const auto& [key, rank] : firstPair) kept.push_back(rank);
        std::sort(kept.begin(), kept.end());
        for (const auto& [di, q, si] : kept) {
            const Flop& d = m_->flops[di];
            const Flop& sf = m_->flops[si];
            evaluated["RDC-010"]++;
            const bool undecided = undecidedBit(sf);
            Diagnostic x = undecided
                ? mk("RDC-010", Severity::Warning, "Unresolved: register " + stripBit(m_->nameOf(sf.q)) + " has an asynchronous reset whose value is not decided and feeds the reset register " +
                     stripBit(m_->nameOf(d.q)) + ": whether it is reset or keeps its value after reset is not established", procLoc(sf).loc, procLoc(sf).instancePath, Confidence::Unresolved)
                : mk("RDC-010", Severity::Warning, "register " + stripBit(m_->nameOf(sf.q)) + " has no reset and feeds the reset register " +
                     stripBit(m_->nameOf(d.q)) + ": after reset the reset register can capture a stale value", procLoc(sf).loc, procLoc(sf).instancePath, Confidence::Heuristic);
            if (undecided) x.proofState = ProofState::NotAttempted;
            x.signals = {stripBit(m_->nameOf(sf.q)), stripBit(m_->nameOf(d.q))};
            add(std::move(x));
        }
    }

    // --- RDC-026: more than one asynchronous control on one register. Set + clear is a real conflict (simultaneous assertion has
    // no defined value; the if/else-if order is the priority); several controls forcing the same value are power-on/functional resets.
    // judged per register bit and reported once per register: one bit set by one control and cleared by another is the conflict
    // A control is one (traced root, polarity) pair: two exact aliases of one source are one control, not a set/clear conflict.
    // A control whose assignment depends on a condition that is not a reset (a conditional hold or fall-through) does not force
    // its value on every path: the bit's conflict is then not established.
    struct Multi {
        const Flop* first = nullptr;
        bool conflict = false, unknown = false, conditional = false;
        std::map<std::pair<NodeId, bool>, uint8_t> ctrlKindMask;   // bit 0: clears, bit 1: sets, bit 2: unknown
    };
    std::map<std::string, Multi> multi;
    for (auto& f : m_->flops) {
        std::vector<size_t> controls;
        std::set<std::pair<NodeId, bool>> distinct;
        for (size_t i = 0; i < f.resets.size(); i++)
            if (distinct.insert({f.resets[i].root.node, f.resets[i].activeLow}).second) controls.push_back(i);
        if (controls.size() < 2) continue;
        auto& mu = multi[stripBit(flopName(f))];
        if (!mu.first) mu.first = &f;
        bool clear = false, set = false;
        for (size_t i : controls) {
            const int k = i < f.resetKinds.size() ? f.resetKinds[i] : 2;
            clear = clear || k == 0;
            set = set || k == 1;
            mu.unknown = mu.unknown || k == 2;
            mu.ctrlKindMask[{f.resets[i].root.node, f.resets[i].activeLow}] |= static_cast<uint8_t>(k == 0 ? 1 : k == 1 ? 2 : 4);
        }
        mu.conflict = mu.conflict || (clear && set);
        // A control whose forced value sits under another condition does not force it on every path (per register: the flag is the
        // control's own branch, not whether its net is a reset somewhere else). An enable evaluated after all controls are inactive is no such condition.
        for (size_t i = 0; i < f.resets.size(); i++)
            if (i < f.resetConditional.size() && f.resetConditional[i]) mu.conditional = true;
    }
    for (auto& [key, mu] : multi) {
        const Flop& f = *mu.first;
        evaluated["RDC-026"]++;
        bool conflict = mu.conflict, unknown = mu.unknown || mu.conditional;
        std::string l;
        for (size_t i = 0; i < f.resets.size(); i++) {
            const uint8_t mask = mu.ctrlKindMask[{f.resets[i].root.node, f.resets[i].activeLow}];
            const char* effect = mask == 1 ? ", clears)" : mask == 2 ? ", sets)" : mask == 3 ? ", clears/sets)" : ")";
            l += (l.empty() ? "" : ", ") + stripBit(m_->nameOf(f.resets[i].root.node)) + (f.resets[i].activeLow ? " (negedge" : " (posedge") + effect;
        }
        // Established only when a clear and a set control are both unconditional; an unknown third control does not undo that conflict.
        const bool established = conflict && !mu.conditional;
        Diagnostic d = mk("RDC-026", conflict || unknown ? Severity::Warning : Severity::Info,
                          "register " + key + " has " + std::to_string(f.resets.size()) + " asynchronous controls [" + l + "]: " +
                          (established ? "a bit is both asynchronously set and cleared, so simultaneous assertion has no defined value (priority = if/else-if order) and releasing them together is a race"
                           : conflict ? "a bit may be both set and cleared, but a control's assignment depends on a condition that is not a reset, so whether it forces its value is not established"
                           : mu.conditional ? "a control's assignment depends on a condition that is not a reset, so whether it forces a value is not established"
                           : unknown ? "the value each control forces is not constant, so priority and simultaneous assertion cannot be judged"
                                     : "all controls force the same value for every bit (power-on and functional resets); their scenarios are not modelled"),
                          procLoc(f).loc, procLoc(f).instancePath, established ? Confidence::Established : (conflict || unknown) ? Confidence::Unresolved : Confidence::Heuristic);
        d.signals = {key};
        if (conflict || unknown) d.proofState = ProofState::NotAttempted;
        add(std::move(d));
        if (unknown) ctx.report.markIncomplete("reset-extraction", "register " + key + " has several asynchronous controls of unknown value");
    }

    // --- RDC-009 for an exported reset: a primary output bit built from an asynchronous reset (or a known reset input) and a
    // functional signal through logic, with no asynchronous pin of this design using it. The consumer is outside the design.
    done.clear();
    for (NodeId o = 0; o < m_->nodes.size(); o++) {
        if (o >= m_->topOutput.size() || !m_->topOutput[o] || !m_->nodes[o].sym || o >= m_->in.size()) continue;
        if (usersOf.count(o) || m_->in[o].empty() || flopBehind(o)) continue;
        // an exact copy chain below the output that a local asynchronous pin uses: that reset is analysed as a root
        bool usedLocally = false;
        {
            NodeId c = o;
            std::unordered_set<NodeId> chain{o};
            while (c < m_->in.size() && m_->in[c].size() == 1) {
                const Edge& e = m_->edges[m_->in[c][0]];
                if (e.type != EdgeType::Comb || !e.exact || !chain.insert(e.from).second) break;
                c = e.from;
                if (usersOf.count(c)) usedLocally = true;
            }
        }
        // a forward exact or inverted copy of the output that a local asynchronous pin uses: analysed as an internal root
        if (!usedLocally && o < m_->out.size()) {
            std::unordered_set<NodeId> fwd{o};
            std::vector<NodeId> fs{o};
            while (!fs.empty() && !usedLocally) {
                const NodeId c = fs.back();
                fs.pop_back();
                if (c >= m_->out.size()) continue;
                for (auto ei : m_->out[c]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type != EdgeType::Comb || !(e.exact || e.flip) || e.ctrl) continue;
                    if (usersOf.count(e.to)) { usedLocally = true; break; }
                    if (fwd.insert(e.to).second) fs.push_back(e.to);
                }
            }
        }
        if (usedLocally) continue;
        std::vector<NodeId> resetLeaves, dataLeaves;
        std::unordered_set<NodeId> seen{o};
        std::vector<NodeId> st{o};
        bool inexact = false;
        while (!st.empty()) {
            const NodeId n = st.back();
            st.pop_back();
            if (n != o && (resetRoots.count(n) || (n < m_->topInput.size() && m_->topInput[n]))) {
                const model::ResetClass cls = resetRoots.count(n) ? model::ResetClass{} : model::classifyResetNode(*m_, n);
                if (resetRoots.count(n) || cls.reset) resetLeaves.push_back(n);
                else if (!cls.undecided) dataLeaves.push_back(n);
                continue;
            }
            if (m_->flopOfQ.count(n)) {
                (syncChain(m_->flopOfQ.at(n)).recognised ? resetLeaves : dataLeaves).push_back(n);
                continue;
            }
            if (n >= m_->in.size()) continue;
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                if (!e.exact) inexact = true;
                if (seen.insert(e.from).second) st.push_back(e.from);
            }
        }
        if (!inexact || resetLeaves.empty() || dataLeaves.empty()) continue;
        const std::string oname = stripBit(m_->nameOf(o));
        // an output with no reset word that combines one reset with functional signals is a reset-qualified data output
        {
            const std::string base = oname.substr(oname.rfind('.') == std::string::npos ? 0 : oname.rfind('.') + 1);
            const std::set<NodeId> distinctResets(resetLeaves.begin(), resetLeaves.end());
            if (distinctResets.size() < 2 && model::classifyResetName(base).verdict == model::ResetName::Verdict::Data) continue;
        }
        if (!done.insert(oname).second) continue;
        const auto leafName = [&](NodeId n) { return stripBit(m_->nameOf(n)); };
        SourceLocation loc;
        std::string hier;
        for (const auto* group : {&dataLeaves, &resetLeaves}) {
            for (auto n : *group) {
                if (const auto it = m_->flopOfQ.find(n); it != m_->flopOfQ.end() && loc.file.empty()) {
                    loc = procLoc(m_->flops[it->second]).loc;
                    hier = procLoc(m_->flops[it->second]).instancePath;
                }
            }
        }
        if (loc.file.empty()) {
            const auto& sym = *m_->nodes[o].sym;
            if (const auto* scope = sym.getParentScope(); scope && sym.location.valid()) {
                const auto& sm = *scope->getCompilation().getSourceManager();
                loc.file = sm.getFullPath(sym.location.buffer()).generic_string();
                loc.line = static_cast<uint32_t>(sm.getLineNumber(sym.location));
                loc.column = static_cast<uint32_t>(sm.getColumnNumber(sym.location));
            }
        }
        // Binate: some register output reaches the output as a multiplexer select (both polarities), and every functional leaf is a
        // register output. A top-level or scan-mode select, or a unate gate (AND/OR), stays Unresolved.
        bool binateRegister = false;
        {
            std::unordered_set<NodeId> sel;
            std::vector<NodeId> ss;
            for (NodeId n : seen) {
                if (n >= m_->in.size() || (n != o && (resetRoots.count(n) || (n < m_->topInput.size() && m_->topInput[n]) || m_->flopOfQ.count(n)))) continue;
                for (auto ei : m_->in[n]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type != EdgeType::Comb || e.op != Op::MuxSel) continue;
                    // the multiplexer must choose between reset signals: some data arm of the same gate reaches a reset leaf
                    bool armsCarryReset = false;
                    for (auto aj : m_->in[n]) {
                        const Edge& arm = m_->edges[aj];
                        if (arm.type != EdgeType::Comb || arm.ctrl || arm.op == Op::MuxSel) continue;
                        std::vector<NodeId> back{arm.from};
                        std::unordered_set<NodeId> visited{arm.from};
                        while (!back.empty() && !armsCarryReset) {
                            const NodeId x = back.back();
                            back.pop_back();
                            if (std::find(resetLeaves.begin(), resetLeaves.end(), x) != resetLeaves.end()) { armsCarryReset = true; break; }
                            if (x >= m_->in.size() || m_->flopOfQ.count(x)) continue;
                            for (auto ek : m_->in[x]) {
                                const Edge& ee = m_->edges[ek];
                                if (ee.type == EdgeType::Comb && visited.insert(ee.from).second) back.push_back(ee.from);
                            }
                        }
                        if (armsCarryReset) break;
                    }
                    if (armsCarryReset && sel.insert(e.from).second) ss.push_back(e.from);
                }
            }
            while (!ss.empty()) {
                const NodeId n = ss.back();
                ss.pop_back();
                if (n >= m_->in.size() || m_->flopOfQ.count(n)) continue;
                for (auto ei : m_->in[n]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && sel.insert(e.from).second) ss.push_back(e.from);
                }
            }
            bool allRegisters = true, anySelect = false;
            for (NodeId n : dataLeaves) {
                if (!m_->flopOfQ.count(n)) allRegisters = false;
                else if (sel.count(n)) anySelect = true;
            }
            binateRegister = allRegisters && anySelect;
        }
        std::string dataNames;   // every functional leaf, so the multiplexer data arms are named
        {
            std::set<std::string> names;
            for (NodeId n : dataLeaves) names.insert(leafName(n));
            size_t shown = 0;
            for (const auto& n : names) if (shown++ < 4) dataNames += (dataNames.empty() ? "" : ", ") + n;
            if (names.size() > 4) dataNames += ", ...";
        }
        evaluated["RDC-009"]++;
        Diagnostic d = mk("RDC-009", Severity::Warning,
                          std::string(binateRegister ? "" : "Unresolved: ") + "exported reset built by logic; consumer outside the design: primary output " + oname +
                          " combines reset [" + leafName(resetLeaves[0]) + "] with functional signal" + std::string(dataLeaves.size() > 1 ? "s" : "") + " [" + dataNames + "], and no asynchronous pin of this design uses it" +
                          (binateRegister ? "; a register output selects between the resets, so the output can glitch whenever that register changes" : ""),
                          loc, hier, binateRegister ? Confidence::Heuristic : Confidence::Unresolved);
        d.signals = {oname, leafName(resetLeaves[0]), leafName(dataLeaves[0])};
        if (!binateRegister) d.proofState = ProofState::NotAttempted;
        d.nextStep = "Register the exported reset in the receiving domain, or document the consumer's glitch and release requirements.";
        add(std::move(d));
        if (!binateRegister) ctx.report.markIncomplete("reset-extraction", "exported reset '" + oname + "' is built by logic and its consumer is outside the design");
    }

    // --- RDC-025: primary outputs that depend on registers without any reset
    done.clear();
    for (NodeId n = 0; n < m_->nodes.size(); n++) {
        if (n >= m_->topOutput.size() || !m_->topOutput[n]) continue;
        std::string oname = stripBit(m_->nameOf(n));
        if (done.count(oname)) continue;
        // trace back through combinational logic to registers
        std::vector<NodeId> st{n};
        std::unordered_set<NodeId> seen{n};
        std::string bad;
        const Flop* badFlop = nullptr;
        // A register the output depends on (the output's own register included) whose reset value is not decided: an X, Z, or
        // non-constant or unanalysed forced value. Reported Unresolved, only when no register is plainly unreset.
        std::string undecided;
        const Flop* undecidedFlop = nullptr;
        while (!st.empty() && bad.empty()) {
            NodeId x = st.back(); st.pop_back();
            auto fit = m_->flopOfQ.find(x);
            if (fit != m_->flopOfQ.end()) {
                const Flop& f = m_->flops[fit->second];
                if (isUnreset(f)) { bad = stripBit(m_->nameOf(x)); badFlop = &f; }
                else if (undecidedBit(f) && !undecidedFlop) { undecided = stripBit(m_->nameOf(x)); undecidedFlop = &f; }
                continue;
            }
            for (auto ei : m_->in[x]) { const Edge& e = m_->edges[ei]; if (e.type == EdgeType::Comb && seen.insert(e.from).second) st.push_back(e.from); }
        }
        if (bad.empty() && undecidedFlop) {
            done.insert(oname);
            evaluated["RDC-025"]++;
            const ProcInfo& undecidedProc = procLoc(*undecidedFlop);
            Diagnostic d = mk("RDC-025", Severity::Warning, "Unresolved: primary output " + oname + " depends on register " + undecided +
                              " whose asynchronous reset value is not decided: the output's state while its side of the interface is in reset is not established",
                              undecidedProc.loc, undecidedProc.instancePath, Confidence::Unresolved);
            d.signals = {oname, undecided};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
            continue;
        }
        if (bad.empty()) continue;
        done.insert(oname);
        evaluated["RDC-025"]++;
        const ProcInfo& badProc = procLoc(*badFlop);
        Diagnostic d = mk("RDC-025", Severity::Warning, "primary output " + oname + " depends on register " + bad + " which has no reset: the output is undefined while its side of the interface is in reset",
                          badProc.loc, badProc.instancePath, Confidence::Heuristic);
        d.signals = {oname, bad};
        d.evidence.push_back({"capture_register", bad, badProc.loc});
        d.nextStep = "Reset the register, or gate/isolate the output while reset is asserted.";
        add(std::move(d));
    }

    // --- RDC-027: SDC exceptions that start at a reset
    // A selector names the reset root by its port bit (`rst_n[3]`, `rst_n[*]`), or a clock object: `get_clocks X` matches the
    // root only when the root is the source of the SDC clock X, never a port that merely shares the name X.
    // Replacement is decided per resolved reset bit (CST-016 round 4, A6): a clock is in force on a bit when one of its sources
    // resolves to that bit and no later create_clock or create_generated_clock without -add resolves to the same bit. Spellings
    // (qualified, unqualified, glob, bus wildcard) do not matter; -add keeps the earlier clock.
    const auto& clocks = ctx.config.constraints.clocks;
    const auto clockInForce = [&](size_t index, NodeId root, const std::string& full) {
        for (size_t j = index + 1; j < clocks.size(); j++) {
            if (clocks[j].add) continue;
            for (const auto& src : clocks[j].sources)
                if (rel_->clockSourceMatches(src, root, full)) return false;
        }
        return true;
    };
    // A bit of a reset vector is named by its declared index (`rst_n[3]` for `[5:2]`), not its offset from the LSB.
    const auto declaredName = [&](NodeId root) {
        const std::string raw = m_->nameOf(root);
        const std::string base = stripBit(raw);
        const auto* sym = m_->nodes[root].sym;
        if (raw == base || !sym || !sym->getType().isIntegral()) return raw;
        const auto range = sym->getType().getFixedRange();
        const int64_t offset = static_cast<int64_t>(m_->nodes[root].bit);
        const int64_t index = range.left > range.right ? range.right + offset : range.right - offset;
        return base + "[" + std::to_string(index) + "]";
    };
    const auto selectorHits = [&](const sdc::ObjectRef& ref, NodeId root, const std::string& full) {
        if (ref.kind == "get_clocks") {
            // Any clock in force on this root: `create_clock -add` can give one port several clocks.
            for (size_t i = 0; i < clocks.size(); i++) {
                for (const auto& source : clocks[i].sources) {
                    const std::string& clockName = clocks[i].name.empty() ? source.pattern : clocks[i].name;
                    if (globMatch(ref.pattern, clockName) && rel_->clockSourceMatches(source, root, full) && clockInForce(i, root, full))
                        return true;
                }
            }
            return false;
        }
        return rel_->nameMatches(ref.kind, ref.pattern, root, full) || rel_->bitMatches(ref.kind, ref.pattern, root);
    };
    // A set_false_path whose -from names only synchroniser-only reset input(s) (with no -through) is the standard constraint.
    const auto isSyncOnlyFalsePath = [&](const sdc::PathException& ex) {
        if (ex.kind != sdc::PathException::Kind::FalsePath || !ex.through.empty() || ex.from.empty()) return false;
        for (const auto& ref : ex.from) {
            if (ref.kind == "get_clocks" || ref.regexp || ref.unsupportedOption) return false;
            bool matchedAny = false;
            for (NodeId n = 0; n < m_->nodes.size(); ++n) {
                const bool isTopIn = n < m_->topInput.size() && m_->topInput[n];
                const bool isTopOut = n < m_->topOutput.size() && m_->topOutput[n];
                const bool isReset = resetRoots.count(n) > 0;
                const bool isFlop = m_->flopOfQ.count(n) > 0;
                if (!isTopIn && !isTopOut && !isReset && !isFlop) continue;
                const std::string full = stripBit(m_->nameOf(n));
                if (!selectorHits(ref, n, full)) continue;
                matchedAny = true;
                if (!isSyncOnlyResetInput(n)) return false;
            }
            if (!matchedAny) return false;
        }
        return true;
    };
    for (auto& ex : ctx.config.constraints.exceptions) {
        if (ex.kind != sdc::PathException::Kind::FalsePath && ex.kind != sdc::PathException::Kind::Multicycle) continue;
        bool checkedExempt = false, exempt = false;
        for (auto root : resetRoots) {
            std::string full = stripBit(m_->nameOf(root));
            bool hit = false;
            for (auto& r : ex.from) hit = hit || selectorHits(r, root, full);
            for (auto& r : ex.through) hit = hit || selectorHits(r, root, full);
            if (!hit) continue;
            if (!checkedExempt) {
                checkedExempt = true;
                exempt = isSyncOnlyFalsePath(ex);
            }
            if (exempt) continue;
            evaluated["RDC-027"]++;
            // A false path removes the timing checks it covers; a multicycle path changes which edge is checked and for how long.
            // Each bit the selector covers is its own finding (the bit is part of the identity).
            const std::string bitName = declaredName(root);
            const bool falsePath = ex.kind == sdc::PathException::Kind::FalsePath;
            Diagnostic d = mk("RDC-027", Severity::Warning, std::string(falsePath ? "set_false_path" : "set_multicycle_path") + " covers reset " + bitName + ": the exception " +
                              (falsePath ? "removes timing checks" : "changes the timing relationship") + ", it does not make the reset-domain crossing safe",
                              ex.loc, {}, Confidence::Established);
            d.signals = {bitName};
            d.identityKey = bitName;
            add(std::move(d));
        }
    }

    // --- RDC-028: one reset signal used with both polarities; resets derived by logic from another reset
    for (auto& [root, users] : usersOf) {
        bool low = false, high = false;
        for (auto u : users) for (auto& r : m_->flops[u].resets) if (r.root.node == root) (r.activeLow ? low : high) = true;
        if (!(low && high)) continue;
        evaluated["RDC-028"]++;
        const Flop& u0 = m_->flops[users[0]];
        Diagnostic d = mk("RDC-028", Severity::Error, "reset " + stripBit(m_->nameOf(root)) + " is used active-low by some registers and active-high by others",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Established);
        d.signals = {stripBit(m_->nameOf(root))};
        add(std::move(d));
    }
    for (auto& [root, users] : usersOf) {
        if (m_->nodes[root].sym == nullptr || flopBehind(root)) continue;
        std::vector<NodeId> leaves;
        std::unordered_set<NodeId> seen{root};
        std::vector<NodeId> st{root};
        bool logic = false, unknownInput = false;
        while (!st.empty()) {
            NodeId n = st.back(); st.pop_back();
            if (n != root && (m_->flopOfQ.count(n) || (n < m_->topInput.size() && m_->topInput[n]))) { leaves.push_back(n); continue; }
            // a node with no modelled driver (black box, undriven, unknown value) on the walk: whether the net is a copy is not decided
            if (n != root && m_->in[n].empty() && m_->constantValue(n) != '0' && m_->constantValue(n) != '1') unknownInput = true;
            // an X or Z literal anywhere in the cone (`n = rst_n & 1'bx`) makes the polarity of the copy unknown too
            if (n != root && unknownLiteralAtBit(m_->nodes[n].sym, m_->nodes[n].bit, m_->collapsed(n))) unknownInput = true;
            for (auto ei : m_->in[n]) { const Edge& e = m_->edges[ei]; if (e.type == EdgeType::Comb) { logic = logic || !e.exact; if (seen.insert(e.from).second) st.push_back(e.from); } }
        }
        const slang::ast::Expression* driver = driverExpression(m_->nodes[root].sym);
        if (unknownLiteralAtBit(m_->nodes[root].sym, m_->nodes[root].bit, m_->collapsed(root))) unknownInput = true;
        // An inverted copy is an exact single-bit inversion of one leaf (`~x`, `!x`: every direct input a flip edge), or `x ^ 1'b1` / `x ~^ 1'b0`.
        // A combination such as `~(x & y)` is not claimed: a constant operand would make it a constant or a copy, which is not read.
        bool inverted = false, exactInversion = true;
        for (auto ei : m_->in[root]) {
            const Edge& e = m_->edges[ei];
            if (e.type != EdgeType::Comb) continue;
            if (e.inv == 1 || e.flip) inverted = true;
            if (!e.flip) exactInversion = false;
        }
        const bool constantNet = m_->constantValue(root) == '0' || m_->constantValue(root) == '1';
        const auto driverOf = [&](const slang::ast::Symbol* net) { return driverExpression(net); };
        if (unknownInput) {
            // The root is an inversion of something the analysis cannot read: the claim is unresolved, not dropped.
            const bool invertingForm = driver && (invertingUnaryForm(*driver, driverOf) || invertingConstantForm(*driver, nullptr, driverOf));
            if ((inverted || invertingForm) && !constantNet) {
                evaluated["RDC-028"]++;
                const Flop& u0 = m_->flops[users[0]];
                Diagnostic d = mk("RDC-028", Severity::Warning, "Unresolved: reset " + stripBit(m_->nameOf(root)) + " is an inversion of logic with an input that has no modelled driver, "
                                  "an undriven or an unknown value: whether it is the inverted copy of one source is not established", procLoc(u0).loc, procLoc(u0).instancePath,
                                  Confidence::Unresolved);
                d.signals = {stripBit(m_->nameOf(root))};
                ctx.report.markIncomplete("reset-extraction", "reset " + stripBit(m_->nameOf(root)) + " polarity relative to its source is unresolved");
                add(std::move(d));
            }
            continue;
        }
        // With the driver expression read, the form decides: a flip edge alone also comes from a masked operand (`~(x & 1'b0)`).
        bool inversionClaim = exactInversion;
        if (driver)
            inversionClaim = invertingUnaryForm(*driver, driverOf) || (leaves.size() == 1 && invertingConstantForm(*driver, m_->nodes[leaves[0]].sym, driverOf));
        if (leaves.size() == 1 && logic && !constantNet && inversionClaim) {
            evaluated["RDC-028"]++;
            const Flop& u0 = m_->flops[users[0]];
            Diagnostic d = mk("RDC-028", Severity::Info, "reset " + stripBit(m_->nameOf(root)) + " is the inverted copy of " + stripBit(m_->nameOf(leaves[0])) +
                              ": its polarity differs from its source; verify the receiving blocks assume the inverted polarity", procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            d.signals = {stripBit(m_->nameOf(root))};
            add(std::move(d));
        }
    }

    // --- RDC-002: reset inputs without a valid input-delay contract (a timing exception alone supplies no reset contract)
    std::map<NodeId, uint32_t> asyncResetInputs;
    for (auto root : resetRoots) {
        if (root < m_->topInput.size() && m_->topInput[root]) {
            asyncResetInputs.emplace(root, usersOf.at(root)[0]);
            continue;
        }
        // Also check primary reset inputs feeding combinational/gated async reset logic (e.g. g = rn & en).
        std::set<NodeId> seen;
        std::vector<NodeId> work{root};
        size_t visits = 0;
        while (!work.empty()) {
            NodeId n = work.back(); work.pop_back();
            if (!seen.insert(n).second || ++visits > 64 || n >= m_->nodes.size()) continue;
            if (n < m_->topInput.size() && m_->topInput[n]) {
                asyncResetInputs.emplace(n, usersOf.at(root)[0]);
                continue;
            }
            if (m_->flopAt(n)) continue;
            if (n >= m_->in.size()) continue;
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::Comb) work.push_back(e.from);
            }
        }
    }
    for (auto [input, fi] : asyncResetInputs) {
        if (isSyncOnlyResetInput(input) || isReleaseSafeInput(input)) continue;
        std::string full = stripBit(m_->nameOf(input));
        bool rejectedMode = false;
        const auto active = rel_->effectiveInputDelays(input, &rejectedMode);
        if (!rejectedMode && !active.empty()) continue;
        evaluated["RDC-002"]++;
        const Flop& u0 = m_->flops[fi];
        Diagnostic d = mk("RDC-002", Severity::Warning, "reset input " + full + " has no constraint: its timing relation to the clock domains it resets is unknown",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
        d.signals = {full};
        d.nextStep = "Declare a valid set_input_delay contract for the reset input against its reference clock, and state how release is synchronised.";
        add(std::move(d));
        ctx.report.markIncomplete("reset-extraction", "reset input " + full + " is unconstrained");
    }
    // ... and top-level inputs used as a synchronous reset (the heuristic candidate): its constraint is an input delay,
    // without which whether it is synchronous to the clock it resets is not declared.
    std::map<NodeId, uint32_t> syncResetInputs;   // input -> first register it resets
    const auto topInputLeaves = [&](NodeId start) {
        std::set<NodeId> inputs, seen;
        std::vector<NodeId> work{start};
        size_t visits = 0;
        while (!work.empty()) {
            NodeId node = work.back(); work.pop_back();
            if (!seen.insert(node).second) continue;
            if (++visits > 64 || node >= m_->nodes.size()) break;
            if (node < m_->topInput.size() && m_->topInput[node]) { inputs.insert(node); continue; }
            if (m_->flopAt(node)) continue;
            if (node >= m_->in.size() || m_->in[node].empty()) continue;
            for (auto ei : m_->in[node]) {
                const Edge& edge = m_->edges[ei];
                if (edge.type != EdgeType::Comb || edge.ctrl || edge.approximate ||
                    !(edge.exact || edge.flip || edge.op == Op::And || edge.op == Op::Or || edge.op == Op::Not)) continue;
                work.push_back(edge.from);
            }
        }
        return inputs;
    };
    for (uint32_t fi = 0; fi < m_->flops.size(); ++fi) {
        const Flop& f = m_->flops[fi];
        bool found = false;
        for (const auto& fact : f.syncResetFacts) {
            for (const NodeId r : topInputLeaves(fact.node)) if (!resetRoots.count(r)) {
                syncResetInputs.emplace(r, fi);
                found = true;
            }
        }
        if (!found && f.syncReset != kNoNode && f.syncReset < m_->topInput.size() && m_->topInput[f.syncReset] && !resetRoots.count(f.syncReset))
            syncResetInputs.emplace(f.syncReset, fi);
    }
    for (auto [input, fi] : syncResetInputs) {
        std::string full = stripBit(m_->nameOf(input));
        bool rejectedMode = false;
        const auto active = rel_->effectiveInputDelays(input, &rejectedMode);
        if (!rejectedMode && !active.empty()) continue;
        evaluated["RDC-002"]++;
        const Flop& u0 = m_->flops[fi];
        Diagnostic d = mk("RDC-002", Severity::Warning, "input " + full + " is used as a synchronous reset but has no input delay: whether it is synchronous to the clock it resets, "
                          "or an asynchronous reset that needs synchronising, is not declared", procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
        d.signals = {full};
        d.nextStep = "Declare the input delay of the reset against the clock it resets, or synchronise it if its source is asynchronous.";
        add(std::move(d));
        ctx.report.markIncomplete("reset-extraction", "synchronous reset input " + full + " is unconstrained");
    }

    // --- explicit "not modelled" markers: RDC-011 (sequencing), RDC-012 / RDC-029 (power intent), RDC-013 (recovery/removal)
    if (!sources.empty()) {
        if (sources.size() > 1) {
            evaluated["RDC-011"]++;
            std::vector<std::string> bits;
            for (NodeId n : sources) bits.push_back(contractBitName(*m_, *rel_, n));
            std::sort(bits.begin(), bits.end());
            bits.erase(std::unique(bits.begin(), bits.end()), bits.end());
            const std::string count = std::to_string(sources.size()) + " independent asynchronous reset sources exist; the order in which they assert and release is not modelled";
            Diagnostic d;
            if (!ctx.config.resetContracts.present) {
                d = mk("RDC-011", Severity::Warning, count + ", and no constraint or intent describes it", {}, {}, Confidence::Unresolved);
            } else {
                // Declared coverage: a clause counts as covering a source only if its status is Bound/Assumed/Established
                // (not Conflict/Unapplied) and it relates that source to ANOTHER counted source.
                auto isActiveStatus = [](ContractStatus s) {
                    return s == ContractStatus::Bound || s == ContractStatus::Assumed || s == ContractStatus::Established;
                };
                std::set<std::string> sourceSet(bits.begin(), bits.end());
                std::set<size_t> cited;
                size_t uncovered = 0;
                for (const auto& bit : bits) {
                    bool covered = false;
                    for (const auto* c : contractsOfBit(contracts_, bit)) {
                        if (c->kind != ContractKind::Group && c->kind != ContractKind::Order) continue;
                        if (!isActiveStatus(c->status)) continue;
                        cited.insert(static_cast<size_t>(c - contracts_.entries.data()));
                        for (const auto& other : c->bits) {
                            if (other != bit && sourceSet.count(other)) { covered = true; break; }
                        }
                    }
                    uncovered += covered ? 0 : 1;
                }
                bool fullyDeclared = true;
                for (size_t i = 0; i < bits.size() && fullyDeclared; i++)
                    for (size_t j = i + 1; j < bits.size() && fullyDeclared; j++) {
                        bool declared = false;
                        for (const auto& e : contracts_.entries) {
                            if (!isActiveStatus(e.status)) continue;
                            if (e.kind == ContractKind::Group)
                                declared = declared || (std::count(e.bits.begin(), e.bits.end(), bits[i]) && std::count(e.bits.begin(), e.bits.end(), bits[j]));
                        }
                        for (const auto* c : ordersBetween(contracts_, bits[i], bits[j]))
                            declared = declared || isActiveStatus(c->status);
                        fullyDeclared = declared;
                    }
                const std::string suffix = fullyDeclared
                    ? " (declared by the environment, not checked)"
                    : !cited.empty()
                        ? "; declared intent covers only part of the reset sequencing and is not checked"
                        : ", and no constraint or intent describes it";
                d = mk("RDC-011", Severity::Warning, count + suffix, {}, {}, Confidence::Unresolved);
                for (size_t i : cited) citeClause(d, contracts_.entries[i]);
                d.evidence.push_back({"site_signal", std::to_string(uncovered) + " of " + std::to_string(bits.size()) +
                                      " traced reset source(s) not covered by any group or release order", {}});
            }
            // No interaction remains: no register bit is controlled by two counted sources, every sampling between registers reset by
            // different sources was exempted (a remaining RDC-004/RDC-005 finding is an interaction), and no primary output combines
            // registers of two sources. A search that cannot finish counts as an interaction.
            bool interaction = !mismatches.empty();
            std::unordered_map<const Flop*, std::set<NodeId>> sourcesCache;   // per register, computed once
            const auto sourcesOfFlop = [&](const Flop& f) -> const std::set<NodeId>& {
                const auto [slot, fresh] = sourcesCache.try_emplace(&f);
                if (!fresh) return slot->second;
                for (const auto& r : f.resets) {
                    const char tied = tiedResetValue(*m_, r.root.node);
                    if (tied == '0' || tied == '1') continue;
                    slot->second.insert(copyRootOf(assertionSource(copyRootOf(r.root.node))));
                }
                return slot->second;
            };
            // A source that is not a primary input (a register, logic or a black box output) is itself driven by other counted
            // sources or by whatever feeds it: they are not independent.
            for (size_t fi = 0; fi < m_->flops.size() && !interaction; fi++) {
                const auto own = sourcesOfFlop(m_->flops[fi]);
                if (own.size() > 1) interaction = true;
                for (NodeId s : own)
                    if (!(s < m_->topInput.size() && m_->topInput[s])) interaction = true;
            }
            // A functional use of a counted source (its value read by a register it does not reset, RDC-019) is an interaction.
            if (!interaction) {
                size_t work = 4000000;
                for (NodeId s : sources) {
                    std::vector<NodeId> fwd{s};
                    std::unordered_set<NodeId> seenFwd{s};
                    while (!fwd.empty() && !interaction) {
                        const NodeId n = fwd.back();
                        fwd.pop_back();
                        if (work == 0 || n >= m_->out.size()) { interaction = work == 0; continue; }
                        --work;
                        for (auto ei : m_->out[n]) {
                            const Edge& e = m_->edges[ei];
                            if (e.type == EdgeType::AsyncCtl || m_->isDeadEdge(e)) continue;
                            if (e.type == EdgeType::Comb) { if (seenFwd.insert(e.to).second) fwd.push_back(e.to); continue; }
                            const auto it = m_->flopOfQ.find(e.to);
                            if (it == m_->flopOfQ.end() || !sourcesOfFlop(m_->flops[it->second]).count(s)) interaction = true;
                        }
                    }
                    if (interaction) break;
                }
            }
            if (!interaction) {
                size_t budget = 4000000;
                std::unordered_map<uint32_t, std::set<NodeId>> flopSources;
                for (NodeId o = 0; o < m_->nodes.size() && !interaction; o++) {
                    if (o >= m_->topOutput.size() || !m_->topOutput[o] || o >= m_->in.size()) continue;
                    std::set<NodeId> reached;
                    std::vector<NodeId> st{o};
                    std::unordered_set<NodeId> seen{o};
                    while (!st.empty() && !interaction) {
                        const NodeId n = st.back();
                        st.pop_back();
                        if (budget == 0 || (n < m_->blackBoxDriven.size() && m_->blackBoxDriven[n])) { interaction = true; break; }
                        --budget;
                        if (sources.count(n)) {   // a reset source itself reaches the output
                            reached.insert(n);
                            if (reached.size() > 1) interaction = true;
                        }
                        if (const auto it = m_->flopOfQ.find(n); it != m_->flopOfQ.end()) {
                            auto [slot, fresh] = flopSources.try_emplace(it->second);
                            if (fresh) slot->second = sourcesOfFlop(m_->flops[it->second]);
                            reached.insert(slot->second.begin(), slot->second.end());
                            if (reached.size() > 1) interaction = true;
                            continue;
                        }
                        if (n >= m_->in.size()) continue;
                        for (auto ei : m_->in[n]) {
                            const Edge& e = m_->edges[ei];
                            if (e.type == EdgeType::Comb && seen.insert(e.from).second) st.push_back(e.from);
                        }
                    }
                }
            }
            if (!interaction) {
                d.severity = Severity::Info;
                d.confidence = Confidence::Heuristic;
                const bool declared = d.message.find("(declared by the environment, not checked)") != std::string::npos;
                d.message = count + (declared ? " (declared by the environment, not checked)" : "") +
                            "; no register bit is controlled by two of them, every sampling between registers reset by different sources is exempt, and no primary output combines them, so no path depends on their order";
                d.evidence.push_back({"trust_model", declared
                    ? "the declared sequencing is the environment's claim and is not checked; this analysis found no data or output path whose behaviour depends on which source asserts or releases first"
                    : "the sources are assumed independent and are not constrained against each other; this analysis found no data or output path whose behaviour depends on which asserts or releases first", {}});
            }
            d.signals = bits;
            for (const auto& b : bits) d.evidence.push_back({"reset_source", b, {}});
            d.nextStep = "Document/verify the reset sequencing; this analysis does not prove any ordering.";
            add(std::move(d));
            if (interaction) pendingMarks_["RDC-011"].emplace_back("reset-extraction", "reset sequencing between " + std::to_string(sources.size()) + " reset sources is not modelled");
        }
        {
            evaluated["RDC-012"]++;
            if (ctx.config.powerIntentModel == "single_always_on") {
                Diagnostic d = mk("RDC-012", Severity::Info, "power intent is declared as power_intent.model = single_always_on: one always-on power domain with no retention, "
                                  "isolation or power switching is assumed, not checked", {}, {}, Confidence::Informational);
                d.evidence.push_back({"trust_model", "the declaration is the user's reviewed claim; UPF and CPF are not read and no retention, isolation or power-domain reset rule is evaluated", {}});
                d.proofState = ProofState::NotAttempted;
                add(std::move(d));
            } else {
                Diagnostic d = mk("RDC-012", Severity::Warning, "no power intent (UPF/CPF) is supported or supplied: retention, isolation and power-domain reset rules are not evaluated",
                                  {}, {}, Confidence::Unresolved);
                add(std::move(d));
                pendingMarks_["RDC-012"].emplace_back("reset-extraction", "RDC-012: power intent is not supported");
            }
        }
        for (auto s : sources) {
            evaluated["RDC-013"]++;
            if (traceTruncated_.count(s)) {
                // The synchroniser chain runs past the trace bound: this node may be a stage, not the asynchronous source.
                Diagnostic d = mk("RDC-013", Severity::Warning, "reset " + stripBit(m_->nameOf(s)) + ": its synchroniser hops exceed the trace bound, so the asynchronous "
                                  "source is unresolved; recovery/removal timing and metastability are not analysed", {}, {}, Confidence::Unresolved);
                d.identityKey = stripBit(m_->nameOf(s));
                d.signals = {stripBit(m_->nameOf(s))};
                d.proofState = ProofState::NotAttempted;
                ctx.report.markIncomplete("reset-extraction", "reset source of '" + stripBit(m_->nameOf(s)) + "' is beyond the synchroniser trace bound");
                add(std::move(d));
                continue;
            }
            Diagnostic d = mk("RDC-013", Severity::Info, "reset " + stripBit(m_->nameOf(s)) + ": recovery/removal timing, minimum pulse width and metastability are physical properties that "
                              "digital RTL cannot show; they are not analysed", {}, {}, Confidence::Informational);
            d.identityKey = stripBit(m_->nameOf(s));
            d.signals = {stripBit(m_->nameOf(s))};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
        }
    }
}

// CST-016: the reset_contracts section bound to the design. Validated once per run; a finding per Assumed, unresolved or
// conflicting clause or source. Without a configured section the rule reports nothing and declares no scope.
void RdcShared::resetContracts(RuleContext& ctx) {
    if (!ctx.config.resetContracts.present) return;
    ctx.report.declareScope("reset-contracts");
    const auto& result = contracts_;
    ctx.report.addInput("reset_contract_validation", model::bindingsToJson(result));
    auto joined = [](const std::vector<std::string>& items, const char* sep) {
        std::string s;
        for (const auto& it : items) s += (s.empty() ? "" : sep) + it;
        return s;
    };
    for (const auto& e : result.entries) {
        evaluated["CST-016"]++;
        if (e.status == model::ContractStatus::Bound) continue;
        const std::string label = e.kind == model::ContractKind::Source ? "reset source" :
            e.kind == model::ContractKind::Group ? "assert_together" : e.kind == model::ContractKind::Order ? "release order" : "state";
        const std::string reasons = joined(e.reasons, "; ");
        const std::string bits = joined(e.bits, ", ");
        const std::string key = std::string("cst016:") + model::contractKindName(e.kind) + ":" + e.id;
        Diagnostic d;
        if (e.status == model::ContractStatus::Assumed) {
            const std::string what = e.kind == model::ContractKind::Order ?
                "release order: " + e.bits[0] + " is released before " + e.bits[1] + " (clock " + e.clock + ", " + e.edge +
                ", at least " + std::to_string(e.minCycles) + " edge(s))" :
                e.kind == model::ContractKind::Group ? "assert_together: [" + bits + "] assert together" :
                "initial state: " + bits + " is asserted at startup";
            d = mk("CST-016", Severity::Info, label + " '" + e.id + "' is an environment assumption, not checked. " + what, {}, {}, Confidence::Informational);
            d.evidence.push_back({"trust_model", "declared environment intent from reset_contracts; the RTL is not checked against it", {}});
        } else if (e.status == model::ContractStatus::Established) {
            d = mk("CST-016", Severity::Info, label + " '" + e.id + "' is satisfied by the RTL: " + reasons, {}, {}, Confidence::Established);
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"trust_model", "ideal RTL and the declared initial state; recovery/removal, metastability and clock availability are not shown", {}});
        } else if (e.status == model::ContractStatus::Conflict) {
            d = mk("CST-016", Severity::Warning, label + " '" + e.id + "' conflicts with the design: " + reasons, {}, {}, Confidence::Established);
        } else {
            const char* state = e.status == model::ContractStatus::RtlRequirement ? "is an RTL requirement that is not yet checked" :
                                e.status == model::ContractStatus::Unapplied ? "is not applied" : "is unresolved";
            d = mk("CST-016", Severity::Warning, label + " '" + e.id + "' " + state + ": " + reasons, {}, {}, Confidence::Unresolved);
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Correct the declaration to an exact supported reset origin, or verify the obligation outside this analysis.";
            ctx.report.markIncomplete("reset-contracts", label + " '" + e.id + "' " + reasons);
        }
        d.identityKey = key;
        d.signals = e.bits;
        d.evidence.push_back({"contract_validation", reasons, {}});
        d.evidence.push_back({"contract_bits", bits, {}});
        for (const auto& x : e.evidence) d.evidence.push_back({"rtl_reset_use", x.text, x.location});
        add(std::move(d));
    }
}

} // namespace sverity::rdc
