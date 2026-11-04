// Native lint on the flow-sensitive procedure analysis (src/analysis/flow.h): every rule here asks a question of the reaching-write
// result instead of walking statements itself.
//   LNT-013 legacy level-sensitive always blocks that leave a variable unassigned on some path (inferred latch)
//   LNT-053 a whole-variable write overwritten before any read
//   LNT-025 potentially conflicting conditional writes when both conditions can hold
//   LNT-044 a variable read before the procedure's first write of it
#include <set>

#include "analysis/flow.h"
#include "domains/model.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;
namespace fl = sverity::flow;

std::string rangeText(const ValueSymbol& s, const fl::Range& r) {
    const Type& t = s.getType();
    uint64_t w = t.isFixedSize() ? t.getBitstreamWidth() : 1;
    if (r.first == 0 && r.second + 1 == w) return std::string(s.name);
    if (r.first == r.second) return std::string(s.name) + "[" + std::to_string(r.first) + "]";
    return std::string(s.name) + "[" + std::to_string(r.second) + ":" + std::to_string(r.first) + "]";
}

// Runs `fn(proc, flow)` for every procedure of the design that lives in user code and is accepted by `pred`.
template <typename Pred, typename Fn>
struct ProcVisit : ASTVisitor<ProcVisit<Pred, Fn>, VisitFlags::AllCanonical> {
    Lint& l;
    Pred pred;
    Fn fn;
    ProcVisit(Lint& lint, Pred p, Fn f) : l(lint), pred(std::move(p)), fn(std::move(f)) {}
    void handle(const ProceduralBlockSymbol& p) {
        if (l.isUser(p.location) && pred(p)) {
            l.count();
            fl::Flow flow(p);
            if (flow.bad)
                l.report(p.location, "procedure could not be fully analysed (slang reported an invalid statement): the result for it is incomplete",
                         {.confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath()});
            fn(p, flow);
        }
        this->visitDefault(p);
    }
};
template <typename Pred, typename Fn>
void forEachProc(Lint& l, Pred pred, Fn fn) {
    ProcVisit<Pred, Fn> v(l, std::move(pred), std::move(fn));
    l.compilation().getRoot().visit(v);
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-013 (legacy blocks): a module-level variable whose final value can still be the one from before the block.
class LegacyLatchRule final : public LintRule {
public:
    LegacyLatchRule() : LintRule("LNT-013", "", true) {}
    void check(Lint& l) override {
        auto legacy = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return p.procedureKind == ProceduralBlockKind::Always && (st == fl::Style::Comb || st == fl::Style::Level);
        };
        forEachProc(l, legacy, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            std::string names;
            for (auto* sym : f.writtenSymbols()) {
                bool local = true;
                for (auto& a : f.accesses) if (a.sym == sym && a.write) local = a.local;
                if (local) continue;
                for (auto& r : f.unassignedAtEnd(*sym)) names += (names.empty() ? "" : ", ") + rangeText(*sym, r);
            }
            if (names.empty() && (f.opaque || f.approximated))
                l.report(p.location, std::string("level-sensitive always block: assignment on every path cannot be proven (") +
                                         (f.opaque ? "it calls a user subroutine or has a timing control" : "a continue or a case with items that cannot match approximates the path analysis") + ")",
                         {.confidence = Confidence::Unresolved, .nextStep = "Review unsupported control flow and ensure every combinational output is assigned on every path; always_comb alone does not remove storage.", .hierarchy = p.getHierarchicalPath()});
            if (!names.empty())
                l.report(p.location, "level-sensitive always block does not assign [" + names + "] on every path: a latch is inferred",
                         {.confidence = f.opaque ? Confidence::Unresolved : Confidence::Heuristic,
                          .nextStep = "Assign a default at the top of the block or cover missing else/default paths; always_comb can diagnose storage but does not repair it.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {names}});
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-053: a whole-variable write replaced by a later whole-variable write that always runs when the first one did, with no
// read in between. Same path guards = Established; the overwrite sits in an enclosing list (the first write is conditional) =
// Heuristic "on at least one path". A user call between the two writes may read the value: Unresolved. A timing control
// between them lets other processes see the first value: no finding. The default-then-override idiom (`z = 0; if (c) z = a;`)
// does not qualify: the overwrite is conditional, so the default stays live on the other path.
class DeadStoreRule final : public LintRule {
public:
    DeadStoreRule() : LintRule("LNT-053") {}
    void check(Lint& l) override {
        forEachProc(l, [](const ProceduralBlockSymbol& p) { return p.procedureKind != ProceduralBlockKind::Final; },
                    [&](const ProceduralBlockSymbol&, const fl::Flow& f) {
            std::set<uint32_t> reported;
            for (auto& k : f.kills) {
                const fl::Access &v = f.accesses[k.victim], &w = f.accesses[k.killer];
                if (!v.exact || !w.exact || v.compound || v.nonblocking != w.nonblocking) continue;
                if (v.inLoop || w.inLoop || k.readBefore || k.observed) continue;
                if (!fl::Flow::guardImplies(v, w)) continue;   // the overwrite is conditional relative to the first write
                if (!reported.insert(k.victim).second) continue;
                bool same = fl::Flow::guardImplies(w, v);
                l.report(v.loc, "assignment to '" + rangeText(*v.sym, v.range) + "' is overwritten before it is read" + (same ? "" : " (on at least one path)"),
                         {.confidence = k.exposed ? Confidence::Unresolved : (same ? Confidence::Established : Confidence::Heuristic),
                          .nextStep = k.exposed ? "A subroutine call lies between the two writes and may read the value; review it." : "Remove the first assignment or use the value.",
                          .signals = {std::string(v.sym->name)}});
            }
        });
    }
};

// Equal literal writes to the same integral range cannot disagree on the final value.
// Evaluate the bound RHS including assignment conversions, not its source spelling.
// Keep opaque calls and different ranges out: neither is an equality proof.
bool sameLiteralValue(const Lint& l, const fl::Access& v, const fl::Access& w) {
    if (!v.asg || !w.asg || v.compound || w.compound || v.range != w.range) return false;
    const auto& a = v.asg->right();
    const auto& b = w.asg->right();
    auto literal = [](const Expression& e) {
        auto kind = e.unwrapImplicitConversions().kind;
        return kind == ExpressionKind::IntegerLiteral || kind == ExpressionKind::UnbasedUnsizedIntegerLiteral;
    };
    if (!literal(a) || !literal(b)) return false;
    auto av = l.evalConst(a), bv = l.evalConst(b);
    return av.isInteger() && bv.isInteger() && !av.integer().hasUnknown() && !bv.integer().hasUnknown() &&
           av.integer().getBitWidth() == bv.integer().getBitWidth() && exactlyEqual(av.integer(), bv.integer());
}

// This is an advisory recognition of text selection, not an exclusivity proof.
// Restrict it to whole packed string-literal writes; numeric state/control chains
// need their own policy. Every conditional write must participate, so an unrelated
// text write cannot lower a mixed data/control conflict.
bool textPriorityChain(const Lint& l, const fl::Flow& f, const fl::Access& target,
                       const std::set<const ValueSymbol*>& controls) {
    if (f.bad || f.opaque || f.approximated || controls.count(target.sym)) return false;
    if (!target.sym->getType().isIntegral() || target.sym->getType().getBitstreamWidth() <= 8) return false;
    std::vector<const fl::Access*> writes;
    for (const auto& a : f.accesses) {
        if (a.inLoop) return false;
        if (!a.write || a.sym != target.sym) continue;
        if (!a.whole || !a.exact || a.compound || !a.asg || a.nonblocking != target.nonblocking) return false;
        const auto& rhs = a.asg->right();
        if (rhs.unwrapImplicitConversions().kind != ExpressionKind::StringLiteral) return false;
        auto value = l.evalConst(rhs);
        if (!value.isInteger() || value.integer().hasUnknown()) return false;
        if (!a.guards->empty()) writes.push_back(&a);
    }
    if (writes.size() < 3) return false;
    for (size_t i = 0; i < writes.size(); ++i)
        for (size_t j = i + 1; j < writes.size(); ++j)
            if (fl::Flow::guardImplies(*writes[i], *writes[j]) ||
                fl::Flow::guardImplies(*writes[j], *writes[i]) || f.exclusive(*writes[i], *writes[j])) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-025: an earlier conditional whole-variable write is overridden by a later conditional one, with no read in between, when the
// conditions are independent and not provably exclusive (different branches of one if/case, or comparisons of one operand with
// different constants). Blocking writes in combinational procedures and nonblocking writes in clocked procedures; the procedure
// kind is reported as evidence.
class OverrideRule final : public LintRule {
public:
    OverrideRule() : LintRule("LNT-025") {}
    void check(Lint& l) override {
        auto want = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return st == fl::Style::Seq || st == fl::Style::Comb || st == fl::Style::Latch || st == fl::Style::Level;
        };
        // Exclude all modelled control sources, including their upstream aliases.
        auto& m = model::getModel(l.context().design);
        std::vector<std::vector<model::NodeId>> upstream(m.nodes.size());
        std::vector<model::NodeId> pending;
        for (const auto& e : m.edges) {
            upstream[e.to].push_back(e.from);
            if (e.ctrl) pending.push_back(e.from);
        }
        for (const auto& flop : m.flops) {
            if (flop.clockSignal != model::kNoNode) pending.push_back(flop.clockSignal);
            for (const auto& reset : flop.resets) pending.push_back(reset.signal);
        }
        std::set<model::NodeId> visited;
        std::set<const ValueSymbol*> controls;
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            if (node == model::kNoNode || !visited.insert(node).second) continue;
            controls.insert(m.nodes[node].sym);
            for (auto from : upstream[node]) pending.push_back(from);
        }
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            uint32_t procedureOrdinal = 0;
            std::string unit;
            if (const auto* scope = p.getParentScope(); scope && scope->asSymbol().kind == SymbolKind::InstanceBody) {
                const auto& definition = scope->asSymbol().as<InstanceBodySymbol>().getDefinition();
                unit = std::to_string(definition.sourceLibrary.name.size()) + ":" + definition.sourceLibrary.name + std::string(definition.name);
                uint32_t ordinal = 0;
                for (const auto& member : scope->members()) {
                    if (member.kind == slang::ast::SymbolKind::ProceduralBlock) ++ordinal;
                    if (&member == &p) { procedureOrdinal = ordinal; break; }
                }
            }
            for (auto& k : f.kills) {
                const fl::Access &v = f.accesses[k.victim], &w = f.accesses[k.killer];
                if (!v.exact || !w.exact || v.compound || v.nonblocking != seq || w.nonblocking != seq) continue;
                if (v.guards->empty() || w.guards->empty() || v.inLoop || w.inLoop) continue;
                if (k.readBefore || k.observed) continue;
                if (fl::Flow::guardImplies(v, w) || fl::Flow::guardImplies(w, v)) continue;   // nested or dominating: LNT-053 / intended priority
                if (f.exclusive(v, w)) continue;
                if (!k.exposed && sameLiteralValue(l, v, w)) continue;
                bool advisory = !k.exposed && textPriorityChain(l, f, v, controls);
                bool unresolved = k.exposed || f.opaque || f.approximated || f.bad;
                l.report(w.loc, "potentially conflicting conditional assignment to '" + rangeText(*v.sym, w.range) + "' overrides the one on line " +
                                    std::to_string(l.sm().getLineNumber(v.loc)) + " if both conditions hold (overlap and intent are not proven)" +
                                    (k.exposed ? " (a subroutine call between them may have read the value)" : "") +
                                    (advisory ? " (string-literal chain assumed to be intended priority; exclusivity is not proved)" : ""),
                         {.severity = advisory && l.option("priority_chains", "info") != "warning" ? Severity::Info : Severity::Warning,
                          .confidence = unresolved ? Confidence::Unresolved : Confidence::Heuristic,
                          .proof = ProofState::NotApplicable,
                          .nextStep = advisory ? "Review the assumed text-selection priority; use --severity LNT-025=WARNING or --option LNT-025.priority_chains=warning to raise it."
                                               : "Review the priority or prove the conditions exclusive; independent if statements may intentionally encode last-write priority.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {std::string(v.sym->name)},
                          .evidence = {{"procedure", seq ? "clocked (nonblocking writes)" : "combinational (blocking writes)", {}}},
                          .groupingUnit = unit, .groupingProcedureOrdinal = procedureOrdinal,
                          .identityKey = std::to_string(l.sm().getLineNumber(v.loc)) + ":" + std::to_string(l.sm().getColumnNumber(v.loc)) +
                              ":" + rangeText(*v.sym, v.range) + ":" + rangeText(*v.sym, w.range)});
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-044: a module-level variable read while every path still holds the value from before the procedure, followed later in the
// procedure by a whole-variable write. Combinational procedures: any write counts (x = x + 1 reads before it assigns).
// Clocked procedures: only blocking writes count (a nonblocking read sees the old value by design) and the write must belong to
// another assignment (cnt = cnt + 1 is the register idiom).
class ReadBeforeAssignRule final : public LintRule {
public:
    ReadBeforeAssignRule() : LintRule("LNT-044") {}
    void check(Lint& l) override {
        auto want = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return st == fl::Style::Comb || st == fl::Style::Seq;
        };
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            std::set<const ValueSymbol*> done;
            for (uint32_t i = 0; i < f.accesses.size(); i++) {
                const fl::Access& r = f.accesses[i];
                if (r.write || r.local || !r.entryOnly || done.count(r.sym)) continue;
                for (uint32_t j = i + 1; j < f.accesses.size(); j++) {
                    const fl::Access& w = f.accesses[j];
                    if (!w.write || w.sym != r.sym || !w.whole || w.compound) continue;
                    if (seq && (w.nonblocking || w.asg == r.asg)) continue;
                    done.insert(r.sym);
                    l.report(r.loc, "'" + std::string(r.sym->name) + "' is read before its first assignment in this block: the read sees the previous value",
                             {.nextStep = "Assign the variable before reading it, or read it through a separate variable.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                    break;
                }
            }
        });
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintDataflowRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<LegacyLatchRule>());
    v.push_back(std::make_unique<DeadStoreRule>());
    v.push_back(std::make_unique<OverrideRule>());
    v.push_back(std::make_unique<ReadBeforeAssignRule>());
    return v;
}

} // namespace sverity
