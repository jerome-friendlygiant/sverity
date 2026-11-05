// Native AST lint: structural drive/coverage and case/generate claims.
//   LNT-008 undriven bits of plain vectors/scalars   LNT-024 undriven or partial output ports
//   LNT-041 incomplete struct/array/interface drive  LNT-042 partial output drive
//   LNT-032 overlapping `case inside` items          LNT-033 unique/priority claims
//   LNT-051 generate branches not elaborated
// Coverage is computed from slang's driver analysis (ValueDriver bounds), never from slang diagnostics.
#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/NetType.h"
#include "slang/numeric/SVInt.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;
using namespace lint;

using Iv = std::pair<uint64_t, uint64_t>;   // inclusive interval of flat bit (or element) positions
using Drivers = std::vector<const ValueDriver*>;

// ---------------------------------------------------------------------------------------------------------------
// Shared helpers: index mapping, interval coverage, type classification, driver collection.

// Declared index of flat position k, following slang's own translation (ConstantRange::translateIndex and the
// reversed storage of unpacked arrays in ElementSelectExpression::evalIndex).
int64_t declIndex(const ConstantRange& r, uint64_t k, bool packed) {
    auto kk = static_cast<int64_t>(k);
    if (packed) return r.isDescending() ? r.lower() + kk : r.upper() - kk;
    return r.isDescending() ? r.left - kk : r.left + kk;
}

// Gaps of `cov` inside [0, width).
std::vector<Iv> complementOf(std::vector<Iv> cov, uint64_t width) {
    std::sort(cov.begin(), cov.end());
    std::vector<Iv> gaps;
    uint64_t next = 0;
    for (auto [lo, hi] : cov) {
        if (lo > next) gaps.push_back({next, lo - 1});
        if (hi >= next) next = hi + 1;
    }
    if (next < width) gaps.push_back({next, width - 1});
    return gaps;
}

// Packed range of a plain (non-aggregate) integral type, if it has one dimension of scalar elements.
std::optional<ConstantRange> packedRangeOf(const Type& t) {
    const Type& c = t.getCanonicalType();
    switch (c.kind) {
        case SymbolKind::ScalarType: return ConstantRange{0, 0};
        case SymbolKind::PredefinedIntegerType: return ConstantRange{static_cast<int32_t>(c.getBitWidth()) - 1, 0};
        case SymbolKind::PackedArrayType: {
            auto& pa = c.as<PackedArrayType>();
            if (pa.elementType.isScalar()) return pa.range;
            return std::nullopt;
        }
        case SymbolKind::EnumType: return packedRangeOf(c.as<EnumType>().baseType);
        default: return std::nullopt;
    }
}

// Aggregates whose coverage is checked per member or element (LNT-041 / LNT-024).
bool isComposite(const Type& t) {
    const Type& c = t.getCanonicalType();
    switch (c.kind) {
        case SymbolKind::PackedStructType:
        case SymbolKind::UnpackedStructType:
        case SymbolKind::PackedUnionType:
        case SymbolKind::UnpackedUnionType:
        case SymbolKind::FixedSizeUnpackedArrayType: return true;
        case SymbolKind::PackedArrayType: return !c.as<PackedArrayType>().elementType.isScalar();
        default: return false;
    }
}

// Containers whose size is dynamic: their driver bounds are not modelled.
bool isDynamicContainer(const Type& t) {
    const Type& c = t.getCanonicalType();
    return c.kind == SymbolKind::DynamicArrayType || c.kind == SymbolKind::QueueType ||
           c.kind == SymbolKind::AssociativeArrayType;
}

// Bit text for plain types ("[7:4], [1]"); flat positions when the type has no simple packed range.
std::string bitsText(const Type& t, const std::vector<Iv>& ivs) {
    auto range = packedRangeOf(t);
    std::string s;
    for (auto [lo, hi] : ivs) {
        if (!s.empty()) s += ", ";
        int64_t a = range ? declIndex(*range, lo, true) : static_cast<int64_t>(lo);
        int64_t b = range ? declIndex(*range, hi, true) : static_cast<int64_t>(hi);
        if (a > b) std::swap(a, b);
        s += a == b ? "[" + std::to_string(a) + "]" : "[" + std::to_string(b) + ":" + std::to_string(a) + "]";
    }
    return range ? s : "bit positions " + s + " (LSB = 0)";
}

// Unresolved wording for a symbol with a write through a non-constant index: bit ranges for plain types, members
// or elements for aggregates (never flat positions).
std::string unknownCoverage(const Type& t, const std::string& name, const std::string& what, const std::vector<Iv>& gaps) {
    std::string msg = "cannot determine which " + std::string(isComposite(t) ? "members" : "bits") + " of " +
                      (what.empty() ? "" : what + " ") + "'" + name + "' are driven: a write uses a non-constant index";
    if (!gaps.empty() && !isComposite(t)) msg += " (possibly " + bitsText(t, gaps) + ")";
    return msg;
}

struct Part {
    std::string label;   // "member 'a'" or "element [2]"
    Iv bits;
};

// Members of a struct or elements of an array, with their flat bit ranges. Empty for unions and scalars.
std::vector<Part> partsOf(const Type& t) {
    std::vector<Part> out;
    const Type& c = t.getCanonicalType();
    switch (c.kind) {
        case SymbolKind::PackedStructType:
        case SymbolKind::UnpackedStructType: {
            auto members = c.kind == SymbolKind::PackedStructType ? c.as<PackedStructType>().membersOfType<FieldSymbol>()
                                                                   : c.as<UnpackedStructType>().membersOfType<FieldSymbol>();
            for (auto& f : members) {
                uint64_t w = f.getType().getSelectableWidth();
                out.push_back({"member '" + std::string(f.name) + "'", {f.bitOffset, f.bitOffset + w - 1}});
            }
            break;
        }
        case SymbolKind::FixedSizeUnpackedArrayType: {
            auto r = c.getFixedRange();
            uint64_t ew = c.getArrayElementType()->getSelectableWidth();
            for (uint64_t k = 0; k < r.fullWidth(); k++)
                out.push_back({"element [" + std::to_string(declIndex(r, k, false)) + "]", {k * ew, k * ew + ew - 1}});
            break;
        }
        case SymbolKind::PackedArrayType: {
            auto& pa = c.as<PackedArrayType>();
            if (pa.elementType.isScalar()) break;
            uint64_t ew = pa.elementType.getSelectableWidth();
            for (uint64_t k = 0; k < pa.range.fullWidth(); k++)
                out.push_back({"element [" + std::to_string(declIndex(pa.range, k, true)) + "]", {k * ew, k * ew + ew - 1}});
            break;
        }
        default: break;
    }
    return out;
}

// Human-readable undriven parts of `t` given the gap intervals.
std::string describeUndriven(const Type& t, const std::vector<Iv>& gaps) {
    auto parts = partsOf(t);
    if (parts.empty()) return "undriven " + bitsText(t, gaps);
    std::string s;
    for (auto& p : parts) {
        uint64_t undriven = 0;
        std::vector<Iv> sub;
        for (auto [glo, ghi] : gaps) {
            uint64_t lo = std::max(glo, p.bits.first), hi = std::min(ghi, p.bits.second);
            if (lo <= hi) {
                sub.push_back({lo, hi});
                undriven += hi - lo + 1;
            }
        }
        if (sub.empty()) continue;
        uint64_t full = p.bits.second - p.bits.first + 1;
        if (!s.empty()) s += "; ";
        s += p.label + (undriven == full ? " is never driven" : " is partly undriven: " + bitsText(t, sub));
    }
    return s;
}

struct Coverage {
    std::vector<Iv> iv;   // bounds of drivers with statically known bits
    bool dynamic = false; // some driver has a non-constant index: coverage cannot be determined
};

bool isStaticDriver(const ValueDriver& d) { return !d.path.empty() && d.path.isFullyStatic(); }

Coverage coverageOf(const Drivers& ds) {
    Coverage c;
    for (auto* d : ds) {
        if (!isStaticDriver(*d)) c.dynamic = true;
        else c.iv.push_back(d->getBounds());
    }
    return c;
}

// Analysis of the elaborated design, or nullptr after recording why the driver rules cannot run.
const AnalysisManager* analysisOf(Lint& l) {
    auto* am = l.context().design.analysis.get();
    if (!am) l.context().degradedReason = "slang driver analysis is unavailable";
    return am;
}

void reportUnresolved(Lint& l, slang::SourceLocation at, std::string message, std::string hierarchy = {}) {
    l.report(at, std::move(message),
             {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
              .nextStep = "Make the index constant, or review the driver manually.", .hierarchy = std::move(hierarchy)});
}

// Module-level nets and variables of every canonical instance body (generate blocks included), output-port
// internals annotated with their port, and every symbol referenced by name in the design.
struct ValueCollector : ASTVisitor<ValueCollector, VisitFlags::AllCanonical> {
    struct Entry {
        const ValueSymbol* sym;
        const PortSymbol* port;   // non-null for the internal symbol of a port
        const InstanceBodySymbol* body;
    };
    std::vector<Entry> vals;
    std::unordered_set<const Symbol*> reads;
    std::unordered_map<const Symbol*, const PortSymbol*> portOf;

    void handle(const InstanceBodySymbol& b) {
        for (auto& p : b.membersOfType<PortSymbol>())
            if (p.internalSymbol) portOf[p.internalSymbol] = &p;
        visitDefault(b);
    }
    void handle(const VariableSymbol& v) {
        add(v);
        visitDefault(v);
    }
    void handle(const NetSymbol& n) {
        add(n);
        visitDefault(n);
    }
    void handle(const NamedValueExpression& e) { reads.insert(&e.symbol); }
    void handle(const HierarchicalValueExpression& e) { reads.insert(&e.symbol); }

    void add(const ValueSymbol& v) {
        auto* sc = v.getParentScope();
        if (!sc) return;
        auto k = sc->asSymbol().kind;
        if (k != SymbolKind::InstanceBody && k != SymbolKind::GenerateBlock) return;
        auto it = portOf.find(&v);
        vals.push_back({&v, it == portOf.end() ? nullptr : it->second, enclosingBody(v)});
    }
};

// True when the expression contains a high-impedance ('z) literal bit (a tri-state driver).
struct ZFinder : ASTVisitor<ZFinder, VisitFlags::AllGood> {
    bool found = false;
    void handle(const IntegerLiteral& e) {
        for (uint32_t i = 0; i < e.getValue().getBitWidth(); i++)
            if (e.getValue()[i].value == logic_t::z.value) found = true;
    }
    void handle(const UnbasedUnsizedIntegerLiteral& e) {
        if (e.getLiteralValue().value == logic_t::z.value) found = true;
    }
};

bool assignsZ(const ValueDriver& d) {
    if (d.containingSymbol->kind != SymbolKind::ContinuousAssign) return false;
    const Expression* e = &d.containingSymbol->as<ContinuousAssignSymbol>().getAssignment();
    if (e->kind == ExpressionKind::Assignment) e = &e->as<AssignmentExpression>().right();
    ZFinder z;
    e->visit(z);
    return z.found;
}

bool isTriNet(const ValueSymbol& s) {
    if (s.kind != SymbolKind::Net) return false;
    switch (s.as<NetSymbol>().netType.netKind) {
        case NetType::Tri:
        case NetType::TriAnd:
        case NetType::TriOr:
        case NetType::Tri0:
        case NetType::Tri1:
        case NetType::TriReg:
        case NetType::WAnd:
        case NetType::WOr: return true;
        default: return false;
    }
}

// Nets and variables written through a non-constant select. Slang's driver list omits such writes or keeps them as
// whole-range drivers, so they are collected from the assignments themselves:
//  - module-indexed: the select uses a module-level value (a port or variable). Slang's coverage of these writes is
//    not reliable, so the symbol's coverage is Unknown.
//  - loop-indexed: the select uses only block-local values (e.g. loop variables). Slang unrolls constant loops exactly
//    and drops the rest, so these matter only where the static drivers leave bits undriven.
bool isModuleValue(const ValueSymbol& s) {
    if (s.kind != SymbolKind::Variable && s.kind != SymbolKind::Net) return false;
    auto* sc = s.getParentScope();
    return sc && (sc->asSymbol().kind == SymbolKind::InstanceBody || sc->asSymbol().kind == SymbolKind::GenerateBlock);
}

struct SelectorRefs : ASTVisitor<SelectorRefs, VisitFlags::AllGood> {
    const ValueSymbol* root = nullptr;
    bool found = false;
    void handle(const NamedValueExpression& e) { check(e.symbol); }
    void handle(const HierarchicalValueExpression& e) { check(e.symbol); }
    void check(const ValueSymbol& s) {
        if (&s != root && isModuleValue(s)) found = true;
    }
};

struct DynWrites {
    std::unordered_set<const ValueSymbol*> moduleIdx, loopIdx;
};

struct DynamicWriteCollector : ASTVisitor<DynamicWriteCollector, VisitFlags::AllCanonical> {
    EvalContext& ec;
    DynWrites out;
    explicit DynamicWriteCollector(EvalContext& e) : ec(e) {}
    void noteLhs(const Expression& lhs) {
        ValuePath::visitPaths(
            lhs, ec,
            [&](const ValuePath& p) {
                auto* root = p.rootSymbol();
                if (!root || p.empty() || p.isFullyStatic()) return;
                SelectorRefs refs;
                refs.root = root;
                p.fullExpr->visit(refs);
                (refs.found ? out.moduleIdx : out.loopIdx).insert(root);
            },
            /* skipSelectors */ true);
    }
    void handle(const AssignmentExpression& a) {
        noteLhs(a.left());
        visitDefault(a);
    }
    // Continuous assignments are visited explicitly so their targets are never skipped.
    void handle(const ContinuousAssignSymbol& ca) {
        auto& e = ca.getAssignment();
        if (e.kind == ExpressionKind::Assignment) noteLhs(e.as<AssignmentExpression>().left());
        visitDefault(ca);
    }
};

DynWrites dynamicWritesOf(Lint& l) {
    ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
    EvalContext ec(ac);
    DynamicWriteCollector c(ec);
    l.compilation().getRoot().visit(c);
    return std::move(c.out);
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-008: a plain vector or scalar with at least one driver whose drivers leave some bits undriven.
// Fully undriven symbols are slang's undriven diagnostics and are not reported here.
class UndrivenBitsRule final : public LintRule {
public:
    // upstream: the fully-undriven half of the title is slang's own diagnostic (mapped to LNT-008).
    UndrivenBitsRule()
        : LintRule("LNT-008", "", true) {}

private:
    void check(Lint& l) override {
        auto* am = analysisOf(l);
        if (!am) return;
        ValueCollector c;
        l.compilation().getRoot().visit(c);
        auto dw = dynamicWritesOf(l);
        for (auto& e : c.vals) {
            const Type& t = e.sym->getType();
            if (e.port || !l.isUser(e.sym->location) || !t.isIntegral() || isComposite(t)) continue;
            auto drivers = am->getDrivers(*e.sym);
            bool modW = dw.moduleIdx.count(e.sym) != 0, locW = dw.loopIdx.count(e.sym) != 0;
            if (drivers.empty() && !modW && !locW) continue;
            l.count();
            auto name = std::string(e.sym->name);
            auto cov = coverageOf(drivers);
            auto gaps = complementOf(cov.iv, t.getSelectableWidth());
            if (modW || cov.dynamic || (locW && !gaps.empty())) {
                reportUnresolved(l, e.sym->location, unknownCoverage(t, name, "", gaps), e.sym->getHierarchicalPath());
                continue;
            }
            if (gaps.empty()) continue;
            l.report(*e.sym, "undriven bit range " + bitsText(t, gaps) + " of '" + name + "' (other bits are driven)",
                     {.nextStep = "Drive the listed bits or tie them off explicitly.", .signals = {name}});
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-024: an output port of a module whose internal symbol has undriven bits or members. A port with no driver
// at all is reported here too (slang's undriven-port diagnostic is a separate, slang-backed report).
class OutputDriveRule final : public LintRule {
public:
    OutputDriveRule() : LintRule("LNT-024") {}

private:
    void check(Lint& l) override {
        auto* am = analysisOf(l);
        if (!am) return;
        ValueCollector c;
        l.compilation().getRoot().visit(c);
        auto dw = dynamicWritesOf(l);
        for (auto& e : c.vals) {
            if (!e.port || e.port->direction != ArgumentDirection::Out || !l.isUser(e.port->location)) continue;
            const Type& t = e.sym->getType();
            if (isDynamicContainer(t)) {   // not modelled: visible as an unresolved result, never silent
                l.count();
                reportUnresolved(l, e.sym->location, "coverage of dynamic, queue or associative output '" + std::string(e.sym->name) + "' is not modelled", e.sym->getHierarchicalPath());
                continue;
            }
            auto name = std::string(e.sym->name);
            auto drivers = am->getDrivers(*e.sym);
            bool modW = dw.moduleIdx.count(e.sym) != 0, locW = dw.loopIdx.count(e.sym) != 0;
            l.count();
            if (drivers.empty() && !modW && !locW) {
                l.report(*e.port, "output '" + name + "' is never driven",
                         {.nextStep = "Drive the output in this module or remove the port.", .signals = {name}});
                continue;
            }
            auto cov = coverageOf(drivers);
            auto gaps = complementOf(cov.iv, t.getSelectableWidth());
            if (modW || cov.dynamic || (locW && !gaps.empty())) {
                reportUnresolved(l, e.port->location, unknownCoverage(t, name, "output", gaps), e.port->getHierarchicalPath());
                continue;
            }
            if (gaps.empty()) continue;
            l.report(*e.port, "output '" + name + "': " + describeUndriven(t, gaps),
                     {.nextStep = "Drive every member or bit of the output, or tie it off.", .signals = {name}});
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-041: module-level struct/union/array variables or nets with some members or elements driven and others not,
// and interface-instance members that are read but never driven.
class AggregateDriveRule final : public LintRule {
public:
    AggregateDriveRule()
        : LintRule("LNT-041") {}

private:
    void check(Lint& l) override {
        auto* am = analysisOf(l);
        if (!am) return;
        ValueCollector c;
        l.compilation().getRoot().visit(c);
        auto dw = dynamicWritesOf(l);
        for (auto& e : c.vals) {
            if (e.port || !l.isUser(e.sym->location)) continue;
            const Type& t = e.sym->getType();
            bool iface = e.body && e.body->getDefinition().definitionKind == DefinitionKind::Interface;
            if (!isComposite(t) && !iface && !isDynamicContainer(t)) continue;
            auto name = std::string(e.sym->name);
            auto drivers = am->getDrivers(*e.sym);
            bool modW = dw.moduleIdx.count(e.sym) != 0, locW = dw.loopIdx.count(e.sym) != 0;
            if (drivers.empty() && !modW && !locW) {
                if (iface && c.reads.count(e.sym)) {
                    l.count();
                    l.report(*e.sym, "interface member '" + name + "' is read but never driven",
                             {.nextStep = "Drive the signal inside the interface or from the connected module.", .signals = {name}});
                }
                continue;
            }
            if (!isComposite(t)) continue;   // plain members of interfaces: LNT-008
            l.count();
            if (isDynamicContainer(t)) {
                reportUnresolved(l, e.sym->location, "coverage of dynamic, queue or associative '" + name + "' is not modelled", e.sym->getHierarchicalPath());
                continue;
            }
            auto cov = coverageOf(drivers);
            auto gaps = complementOf(cov.iv, t.getSelectableWidth());
            if (modW || cov.dynamic || (locW && !gaps.empty())) {
                reportUnresolved(l, e.sym->location, unknownCoverage(t, name, "", gaps), e.sym->getHierarchicalPath());
                continue;
            }
            if (gaps.empty()) continue;
            l.report(*e.sym, "incomplete assignment to '" + name + "': " + describeUndriven(t, gaps),
                     {.nextStep = "Assign every member or element, or document why the rest is unused.", .signals = {name}});
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-009, instance outputs: an output port and another continuous driver on the same bits (called by LNT-009's rule).
void outputPortOverlaps(Lint& l) {
    auto* am = analysisOf(l);
    if (!am) return;
    ValueCollector c;
    l.compilation().getRoot().visit(c);
    auto dw = dynamicWritesOf(l);
    for (auto& e : c.vals) {
        if (!l.isUser(e.sym->location)) continue;
        auto drivers = am->getDrivers(*e.sym);
        Drivers conts;
        bool anyOutput = false;
        for (auto* d : drivers) {
            anyOutput = anyOutput || d->flags.has(DriverFlags::OutputPort);
            if (d->kind == DriverKind::Continuous && !d->isInputPort()) conts.push_back(d);
        }
        if (!anyOutput) continue;
        auto name = std::string(e.sym->name);
        bool dyn = dw.moduleIdx.count(e.sym) != 0 || dw.loopIdx.count(e.sym) != 0;
        if (dyn)
            reportUnresolved(l, e.sym->location, "output port drive into '" + name + "' is combined with a write through a non-constant index: driver overlap is not determinable",
                             e.sym->getHierarchicalPath());
        for (auto* d : drivers)
            if (!isStaticDriver(*d)) {
                dyn = true;
                reportUnresolved(l, d->getSourceRange().start(), "output port drive into '" + name + "' uses a non-constant index: driver overlap is not determinable",
                                 e.sym->getHierarchicalPath());
            }
        if (dyn || isTriNet(*e.sym)) continue;
        auto isOut = [](const ValueDriver* d) { return d->flags.has(DriverFlags::OutputPort); };
        for (size_t j = 1; j < conts.size(); j++)
            for (size_t i = 0; i < j; i++) {
                const ValueDriver *a = conts[i], *b = conts[j];
                if (!isOut(a) && !isOut(b)) continue;
                if (assignsZ(*a) || assignsZ(*b)) continue;
                auto ba = a->getBounds(), bb = b->getBounds();
                uint64_t lo = std::max(ba.first, bb.first), hi = std::min(ba.second, bb.second);
                if (lo > hi) continue;
                // report at the later source position so the finding does not depend on driver order
                auto at = a->getSourceRange().start().offset() > b->getSourceRange().start().offset() ? a : b;
                l.report(at->getSourceRange().start(),
                         "overlapping drive of '" + name + "' bits " + bitsText(e.sym->getType(), {{lo, hi}}) +
                             ": an output port and another continuous driver",
                         {.nextStep = "Connect disjoint bit ranges, or make the net a tri-state net if the drivers are meant to share it.",
                          .hierarchy = e.sym->getHierarchicalPath(), .signals = {name}});
                j = conts.size();   // one finding per net
                break;
            }
    }
}

// LNT-042: an output port connected to part of a net, leaving bits of the net
// without any other driver.
class PortDriveRule final : public LintRule {
public:
    PortDriveRule() : LintRule("LNT-042") {}

private:
    void check(Lint& l) override {
        auto* am = analysisOf(l);
        if (!am) return;
        ValueCollector c;
        l.compilation().getRoot().visit(c);
        auto dw = dynamicWritesOf(l);
        for (auto& e : c.vals) {
            if (!l.isUser(e.sym->location)) continue;
            auto drivers = am->getDrivers(*e.sym);
            Drivers outs;
            for (auto* d : drivers)
                if (d->flags.has(DriverFlags::OutputPort)) outs.push_back(d);
            if (outs.empty()) continue;
            l.count();
            auto name = std::string(e.sym->name);
            bool dyn = dw.moduleIdx.count(e.sym) != 0 || dw.loopIdx.count(e.sym) != 0;
            if (dyn)
                reportUnresolved(l, e.sym->location, "output port drive into '" + name + "' is combined with a write through a non-constant index: partial drive is not determinable",
                                 e.sym->getHierarchicalPath());
            for (auto* d : drivers)
                if (!isStaticDriver(*d)) {
                    dyn = true;
                    reportUnresolved(l, d->getSourceRange().start(),
                                     "output port drive into '" + name + "' uses a non-constant index: partial drive is not determinable",
                                     e.sym->getHierarchicalPath());
                }
            if (dyn) continue;

            const uint64_t width = e.sym->getType().getSelectableWidth();
            // Partial drive through hierarchy: an output port covers a slice and nothing else drives the rest.
            auto cov = coverageOf(drivers);
            if (cov.dynamic) continue;
            for (auto* o : outs) {
                auto b = o->getBounds();
                if (b.first == 0 && b.second + 1 == width) continue;
                auto gaps = complementOf(cov.iv, width);
                if (gaps.empty()) break;
                l.report(o->getSourceRange().start(),
                         "partial drive through hierarchy: output port drives " + bitsText(e.sym->getType(), {b}) + " of '" + name +
                             "' and " + bitsText(e.sym->getType(), gaps) + " has no driver",
                         {.severity = Severity::Info, .confidence = Confidence::Established,
                          .nextStep = "Confirm the remaining bits are intentionally undriven.", .hierarchy = e.sym->getHierarchicalPath(),
                          .signals = {name}});
                break;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Case-item model shared by LNT-032 and LNT-033. An arm is either a pattern (value/care bits; casez/casex wildcards
// are not care bits) or an inclusive range (case inside). Items that can never match a two-state value are `never`.
struct Arm {
    const Expression* expr = nullptr;
    bool range = false, never = false;
    uint64_t val = 0, care = 0, lo = 0, hi = 0;
};

// Builds the arm for an item; false when the item is not a constant this analysis models.
bool buildArm(Lint& l, const Expression& e, CaseStatementCondition cond, uint32_t width, Arm& a) {
    a.expr = &e;
    if (cond == CaseStatementCondition::Inside) {
        const Expression* lo = &e;
        const Expression* hi = &e;
        if (e.kind == ExpressionKind::ValueRange) {
            lo = &e.as<ValueRangeExpression>().left();
            hi = &e.as<ValueRangeExpression>().right();
        }
        auto cl = l.evalConst(*lo), ch = l.evalConst(*hi);
        if (!cl.isInteger() || !ch.isInteger()) return false;
        auto ul = cl.integer().as<uint64_t>(), uh = ch.integer().as<uint64_t>();
        if (!ul || !uh) return false;
        a.range = true;
        a.lo = std::min(*ul, *uh);
        a.hi = std::max(*ul, *uh);
        return true;
    }
    auto cv = l.evalConst(e);
    if (!cv.isInteger() || width > 64) return false;
    const SVInt& v = cv.integer();
    for (uint32_t i = 0; i < width; i++) {
        logic_t bit = i < v.getBitWidth() ? v[i] : logic_t(0);
        uint64_t m = uint64_t(1) << i;
        bool isX = bit.value == logic_t::x.value, isZ = bit.value == logic_t::z.value;
        if (!isX && !isZ) {
            a.care |= m;
            if (bit.value == 1) a.val |= m;
        }
        else if ((isZ && cond == CaseStatementCondition::WildcardJustZ) ||
                 ((isX || isZ) && cond == CaseStatementCondition::WildcardXOrZ)) {
            // wildcard bit: matches anything
        }
        else {
            a.never = true;   // an unknown bit that is not a wildcard never matches a two-state value
        }
    }
    return true;
}

bool overlaps(const Arm& a, const Arm& b) {
    if (a.never || b.never) return false;
    if (a.range && b.range) return a.lo <= b.hi && b.lo <= a.hi;
    if (a.range != b.range) return false;
    return ((a.val ^ b.val) & a.care & b.care) == 0;
}

bool matches(const Arm& a, uint64_t v) {
    if (a.never) return false;
    if (a.range) return a.lo <= v && v <= a.hi;
    return ((v ^ a.val) & a.care) == 0;
}

bool anyMatch(const std::vector<Arm>& arms, uint64_t v) {
    for (auto& a : arms)
        if (matches(a, v)) return true;
    return false;
}

std::string binText(uint64_t v, uint32_t w) {
    std::string s = std::to_string(w) + "'b";
    for (uint32_t i = w; i-- > 0;) s += ((v >> i) & 1) ? '1' : '0';
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-032: overlapping `case inside` items. Slang reports duplicate, overlapping and unreachable items of
// case/casez/casex (case-dup, case-overlap, case-unreachable); `inside` items are not covered by slang, so they
// are checked here.
class InsideOverlapRule final : public LintRule {
public:
    // upstream: casez/casex/case overlap, duplicate and unreachable items are slang's (case-dup/overlap/unreachable).
    InsideOverlapRule() : LintRule("LNT-032", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const CaseStatement& c) {
            if (c.condition == CaseStatementCondition::Inside && l.isUser(c.sourceRange.start())) check(c);
            visitDefault(c);
        }
        void check(const CaseStatement& c) {
            l.count();
            uint32_t w = c.expr.unwrapImplicitConversions().type->getBitWidth();
            std::vector<Arm> arms;
            bool unresolved = false;
            for (auto& g : c.items)
                for (auto* e : g.expressions) {
                    Arm a;
                    if (buildArm(l, *e, c.condition, w, a)) arms.push_back(a);
                    else unresolved = true;
                }
            if (unresolved) {
                l.report(c.sourceRange.start(), "'case inside' overlap not determinable: an item is not a constant",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                          .nextStep = "Review the items for overlap manually."});
                return;
            }
            for (size_t j = 1; j < arms.size(); j++)
                for (size_t i = 0; i < j; i++)
                    if (overlaps(arms[i], arms[j])) {
                        l.report(arms[j].expr->sourceRange.start(), "'inside' item overlaps a previous item: a value matches both (first match wins)",
                                 {.nextStep = "Make the items disjoint, or drop the redundant item."});
                        break;
                    }
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-033: `unique`, `unique0` and `priority` claims on case and if statements.
//  - unique/unique0 case: two items match the same value -> ERROR (the claim is statically false)
//  - unique/priority case without default: uncovered values (or X/Z of a four-state selector) -> WARNING
//  - unique/priority if without final else -> WARNING; unique if with two constant true conditions -> ERROR
//  - non-constant items or conditions, or wide selectors -> INFO Unresolved
class UniqueClaimRule : public LintRule {
public:
    explicit UniqueClaimRule(bool overlap) : LintRule(overlap ? "LNT-033" : "LNT-081"), overlapRule(overlap) {}
private:
    bool overlapRule;
    static constexpr uint32_t kMaxEnumeratedWidth = 16;

    static std::string keyword(UniquePriorityCheck c) {
        return c == UniquePriorityCheck::Unique ? "unique" : c == UniquePriorityCheck::Unique0 ? "unique0" : "priority";
    }

    void checkCase(Lint& l, const CaseStatement& c) {
        const UniquePriorityCheck chk = c.check;
        const std::string kw = keyword(chk) + " case";
        const auto& sel = c.expr.unwrapImplicitConversions();
        const uint32_t w = sel.type->getBitWidth();
        std::vector<Arm> arms;
        bool unresolved = w > 64;
        for (auto& g : c.items)
            for (auto* e : g.expressions) {
                Arm a;
                if (!unresolved && buildArm(l, *e, c.condition, w, a)) arms.push_back(a);
                else unresolved = true;
            }
        if (unresolved) {
            const bool checkApplies = overlapRule ? chk != UniquePriorityCheck::Priority
                                                  : chk != UniquePriorityCheck::Unique0 && !c.defaultCase;
            if (checkApplies)
                l.report(c.sourceRange.start(), "'" + kw + "' claim not statically checkable: a case item is not a constant or the selector is wider than 64 bits",
                         {.severity = Severity::Info, .confidence = Confidence::Unresolved,
                          .nextStep = "Check the claim with simulation or formal analysis."});
            return;
        }
        if (overlapRule && chk != UniquePriorityCheck::Priority) {
            for (size_t j = 1; j < arms.size(); j++)
                for (size_t i = 0; i < j; i++)
                    if (overlaps(arms[i], arms[j])) {
                        l.report(arms[j].expr->sourceRange.start(),
                                 "'" + kw + "' claim is statically false: this item matches a value that an earlier item also matches",
                                 {.severity = Severity::Error, .nextStep = "Make the items mutually exclusive, or use 'priority case' if first-match semantics are intended."});
                        break;
                    }
        }
        if (overlapRule || chk == UniquePriorityCheck::Unique0 || c.defaultCase) return;
        std::vector<std::string> missing;
        if (sel.type->isEnum()) {
            for (auto& ev : sel.type->getCanonicalType().as<EnumType>().values()) {
                auto& val = ev.getValue();
                if (!val.isInteger()) continue;
                auto iv = val.integer().as<uint64_t>();
                if (iv && !anyMatch(arms, *iv)) missing.push_back(std::string(ev.name));
            }
        }
        else if (w <= kMaxEnumeratedWidth) {
            for (uint64_t v = 0; v < (uint64_t(1) << w); v++)
                if (!anyMatch(arms, v) && missing.size() < 4) missing.push_back(binText(v, w));
        }
        else {
            l.report(c.sourceRange.start(), "'" + kw + "' coverage not checkable: " + std::to_string(w) + "-bit selector is not enumerated",
                     {.severity = Severity::Info, .confidence = Confidence::Unresolved, .nextStep = "Add a default item or check coverage with formal analysis."});
            return;
        }
        if (sel.type->isFourState()) missing.push_back("X/Z");   // unknown selector bits match no non-wildcard item
        if (missing.empty()) return;
        std::string list;
        for (auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        l.report(c.sourceRange.start(), "'" + kw + "' claim cannot be satisfied for uncovered value(s) " + list + " (no default item)",
                 {.nextStep = "Add a default item, or cover the listed values."});
    }

    void checkIf(Lint& l, const ConditionalStatement& s) {
        const std::string kw = keyword(s.check) + " if";
        std::vector<const Expression*> conds;
        bool hasElse = true;
        const ConditionalStatement* cur = &s;
        for (;;) {
            for (auto& c : cur->conditions) conds.push_back(c.expr.get());
            const Statement* f = cur->ifFalse;
            if (!f) {
                hasElse = false;
                break;
            }
            if (f->kind != StatementKind::Conditional || f->as<ConditionalStatement>().check != UniquePriorityCheck::None) break;
            cur = &f->as<ConditionalStatement>();
        }
        if (!overlapRule && !hasElse && (s.check == UniquePriorityCheck::Unique || s.check == UniquePriorityCheck::Priority))
            l.report(s.sourceRange.start(), "'" + kw + "' chain has no final else: the claim fails when no condition is true",
                     {.nextStep = "Add a final else branch, or use a unique0 check if no-match is allowed.", .identityKey = "missing-else"});
        if (!overlapRule && (s.check != UniquePriorityCheck::Unique && s.check != UniquePriorityCheck::Unique0)) return;
        if (overlapRule && (s.check != UniquePriorityCheck::Unique && s.check != UniquePriorityCheck::Unique0)) return;
        int trueCount = 0;
        bool allConst = true;
        for (auto* e : conds) {
            auto cv = l.evalConst(*e);
            if (!cv) {
                allConst = false;
                break;
            }
            if (cv.isTrue()) trueCount++;
        }
        if (!allConst) {
            l.report(s.sourceRange.start(), "'" + kw + "' claim not statically checkable: a condition is not a constant",
                     {.severity = Severity::Info, .confidence = Confidence::Unresolved, .nextStep = "Check the claim with simulation or formal analysis.", .identityKey = "nonconstant-claim"});
        }
        else if (trueCount >= 2) {
            l.report(s.sourceRange.start(), "'" + kw + "' claim is statically false: " + std::to_string(trueCount) + " conditions are true",
                     {.severity = Severity::Error, .nextStep = "Make the conditions mutually exclusive."});
        }
    }

    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        UniqueClaimRule& rule;
        explicit V(Lint& lint, UniqueClaimRule& owner) : l(lint), rule(owner) {}
        void handle(const CaseStatement& c) {
            if (c.check != UniquePriorityCheck::None && l.isUser(c.sourceRange.start())) {
                l.count();
                rule.checkCase(l, c);
            }
            visitDefault(c);
        }
        void handle(const ConditionalStatement& s) {
            if (s.check != UniquePriorityCheck::None && l.isUser(s.sourceRange.start())) {
                l.count();
                rule.checkIf(l, s);
            }
            visitDefault(s);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l, *this); }
};

class UncoveredClaimRule final : public UniqueClaimRule {
public: UncoveredClaimRule() : UniqueClaimRule(false) {}
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-051: generate branches that are not elaborated in the selected configuration (INFO per branch), and
// constructs in which no branch is elaborated (WARNING).
class GenerateBranchRule final : public LintRule {
public:
    GenerateBranchRule()
        : LintRule("LNT-051") {}

private:
    struct Construct {
        slang::SourceLocation loc;
        int total = 0, elaborated = 0;
    };

    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<std::pair<const void*, uint32_t>, Construct> constructs;
        explicit V(Lint& lint) : l(lint) {}

        void handle(const GenerateBlockSymbol& g) {
            if (g.branchKind != GenerateBranchKind::LoopIteration && l.isUser(g.location)) {
                auto& k = constructs[{static_cast<const void*>(g.getParentScope()), g.constructIndex}];
                if (k.total++ == 0) k.loc = g.location;
                if (g.isUninstantiated) report(g);
                else k.elaborated++;
            }
            visitDefault(g);
        }

        void report(const GenerateBlockSymbol& g) {
            std::string kind, why;
            std::string identity = "branch:" + std::to_string(static_cast<int>(g.branchKind)) + ":" + std::string(g.name);
            switch (g.branchKind) {
                case GenerateBranchKind::IfTrue:
                case GenerateBranchKind::IfFalse: {
                    kind = g.branchKind == GenerateBranchKind::IfTrue ? "if" : "else";
                    if (auto* ce = g.getConditionExpression()) {
                        auto cv = l.evalConst(*ce);
                        if (cv) { why = " (condition evaluates to " + cv.toString() + ")"; identity += ":" + cv.toString(); }
                    }
                    break;
                }
                case GenerateBranchKind::CaseDefault: kind = "case default"; why = " (not selected by the case value)"; break;
                default: kind = "case item"; why = " (not selected by the case value)"; break;
            }
            std::string name = g.isUnnamed ? "(unnamed)" : std::string(g.name);
            l.count();
            l.report(g.location, "generate " + kind + " branch '" + name + "' is not elaborated in this configuration" + why,
                     {.severity = Severity::Info, .confidence = Confidence::Informational,
                      .nextStep = "Check the other configurations this block is meant to support.", .hierarchy = g.getHierarchicalPath(), .identityKey = std::move(identity)});
        }
    };

    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
        for (auto& [key, k] : v.constructs)
            if (k.elaborated == 0 && k.total > 0)
                l.report(k.loc, "no generate branch of this construct is elaborated in this configuration",
                         {.nextStep = "The construct is dead for the selected parameters; check the parameter values.", .identityKey = "construct"});
    }
};

} // namespace

void reportOutputPortOverlaps(lint::Lint& l) { outputPortOverlaps(l); }

std::vector<std::unique_ptr<Rule>> makeLintStructRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<UndrivenBitsRule>());
    v.push_back(std::make_unique<OutputDriveRule>());
    v.push_back(std::make_unique<AggregateDriveRule>());
    v.push_back(std::make_unique<PortDriveRule>());
    v.push_back(std::make_unique<InsideOverlapRule>());
    v.push_back(std::make_unique<UniqueClaimRule>(true));
    v.push_back(std::make_unique<UncoveredClaimRule>());
    v.push_back(std::make_unique<GenerateBranchRule>());
    return v;
}

} // namespace sverity
