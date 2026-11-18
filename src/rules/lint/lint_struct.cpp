// Native AST lint: structural drive/coverage and case/generate claims.
//   LNT-008 undriven bits of plain vectors/scalars   LNT-024 undriven or partial output ports
//   LNT-041 incomplete struct/array/interface drive  LNT-042 partial output drive
//   LNT-032 overlapping `case inside` items          LNT-033 unique/priority claims
//   LNT-051 generate branches not elaborated
// Coverage is computed from slang's driver analysis (ValueDriver bounds), never from slang diagnostics.
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "synth_pragma.h"
#include "domains/model.h"
#include "rules/lint/loop_facts.h"
#include "rules/lint/value_range.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/EvalContext.h"
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
std::string unknownCoverage(const Type& t, const std::string& name, const std::string& what, const std::vector<Iv>& gaps, bool floorOnly = false) {
    std::string msg = "cannot determine which " + std::string(isComposite(t) ? "members" : "bits") + " of " +
                      (what.empty() ? "" : what + " ") + "'" + name + "' are driven: " +
                      (floorOnly ? "a write sits in code the driver analysis drops (after a `continue` that runs, or in a loop it takes for never executing)"
                                 : "a write uses a non-constant index");
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
struct ValueCollector : HardwareVisitor<ValueCollector, VisitFlags::AllCanonical> {
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
    const std::vector<LoopValues>* proven = nullptr;   // loops being followed: a procedure temporary they count is the loop's own, not design state
    const std::unordered_set<const ValueSymbol*>* temps = nullptr;
    bool found = false;
    void handle(const NamedValueExpression& e) { check(e.symbol); }
    void handle(const HierarchicalValueExpression& e) { check(e.symbol); }
    void check(const ValueSymbol& s) {
        if (&s == root || !isModuleValue(s)) return;
        if (proven && temps && temps->count(&s) &&
            std::any_of(proven->begin(), proven->end(), [&](const LoopValues& v) { return v.var == &s; }))
            return;
        found = true;
    }
};

struct DynWrites {
    std::unordered_set<const ValueSymbol*> moduleIdx, loopIdx;
    std::unordered_set<const ValueSymbol*> unprovenLoop;   // loop-indexed writes that no proof below accounts for
    // Module-indexed writes whose bits the value range of the index decides (LNT-041 only): the bits every such write can reach, and the
    // symbols with a module-indexed write that this does not decide (a guard may narrow the index, the shape is not supported).
    std::unordered_map<const ValueSymbol*, std::vector<Iv>> rangeBits;
    std::unordered_set<const ValueSymbol*> moduleUndecided;
    // Symbols with a module-indexed write placed by the index's type range where the index is not a free input port: the range bounds the
    // bits the write can reach (so a member outside it is certainly undriven), but the values the index takes are not known, so a
    // member inside it is possibly driven, not proven driven.
    std::unordered_set<const ValueSymbol*> rangeUnproven;
    std::unordered_map<const ValueSymbol*, std::vector<Iv>> provenBits;   // bits that proven loop-indexed writes drive
    // The symbol named by each write whose index is still not constant after slang's unrolling. Slang gives such a write a
    // driver of the whole selected range (or of whatever it could evaluate), which proves nothing about the bits written, so
    // these drivers never count as coverage; only provenBits place bits for them.
    std::vector<slang::SourceLocation> dynamicWriteRoots;
    // Slang also reports a write that sits in a subroutine at each call site, as a driver of the whole symbol when it could not
    // place the bits. These are the call sites, and the symbols written through a non-constant index inside a subroutine.
    std::vector<slang::SourceLocation> callSites;
    std::vector<slang::SourceLocation> argSites;   // where the actual arguments of user calls start: a `ref` actual is a driver slang lists though the callee may only read it
    std::unordered_set<const ValueSymbol*> subroutineDynamicWrites;
    // The syntactic floor under slang's driver list: the bits of every static write in code the design runs. slang drops writes after
    // a decided `continue` and in loops it takes for never executing, so a symbol whose drivers do not cover such a write has a
    // possible driver slang does not list (markFloor).
    std::unordered_map<const ValueSymbol*, std::vector<Iv>> staticWrites;
    std::unordered_set<const ValueSymbol*> floorOnly;   // possible drivers found by the floor alone
    // Symbols written in dead code (after a certain jump, under a constant-false condition, in a loop that runs zero times). One with
    // no driver and no other write is never driven, though the source assigns it, so slang's own "never assigned" does not fire.
    std::unordered_set<const ValueSymbol*> deadWrites;
    // Symbols with a write that runs: slang lists one driver per procedure and bit range, at the first write, so a dead first write
    // (`if (!en) r = a; else r = b;` with `en` tied to 1) must not stand for the later live one.
    std::unordered_set<const ValueSymbol*> liveWrites;
    // The bits those live writes drive (a whole-object write is the whole range). A driver slang lists at a dead write is no hardware, so a
    // symbol with dead writes takes its coverage from the drivers that are not dead plus these bits, not from the symbol-wide "some write is live".
    std::unordered_map<const ValueSymbol*, std::vector<Iv>> liveBits;
    // Symbols written only where a condition some instances tie to a constant and others leave free guards the write: the shared body
    // cannot say which instances write them, and one that is tied off never drives its copy.
    std::unordered_set<const ValueSymbol*> maybeOff;
    // Symbols some statement writes whole, outside every condition, loop and subroutine of a procedure that runs to its end (a default
    // assignment before the `case`): every instance drives them whatever its ties are, so a tied guard further down does not leave them undriven.
    std::unordered_set<const ValueSymbol*> alwaysWritten;
    // Where each dead write starts, with the procedural block or continuous assignment (one per elaborated instance) that holds it.
    // A driver slang lists there is not hardware either, but only in that instance: in a generate loop one source location is dead
    // for some genvar values and live for others. A null container (a subroutine body) matches by location alone.
    std::vector<std::pair<slang::SourceLocation, const Symbol*>> deadSites;
};

// A symbol whose only writes are dead code: reported under LNT-008 as never driven (slang reports one with no write at all).
bool atDeadSite(const DynWrites& dw, const ValueDriver& d) {
    return std::any_of(dw.deadSites.begin(), dw.deadSites.end(), [&](const auto& s) {
        return s.first == d.getSourceRange().start() && (!s.second || s.second == d.containingSymbol);
    });
}

bool onlyDeadWrites(const DynWrites& dw, const ValueSymbol& sym, const Drivers& drivers) {
    if (!dw.deadWrites.count(&sym) || dw.liveWrites.count(&sym) || dw.moduleIdx.count(&sym) || dw.loopIdx.count(&sym)) return false;
    return std::all_of(drivers.begin(), drivers.end(), [&](const ValueDriver* d) { return atDeadSite(dw, *d); });
}

// The drivers a synthesis tool sees: one written inside a `translate_off` region is not built. `removed` is set when any is dropped.
Drivers synthesisDrivers(Lint& l, const PragmaIndex& pragmas, const Drivers& drivers, bool& removed) {
    removed = false;
    if (pragmas.regions.empty()) return drivers;
    Drivers kept;
    for (const auto* d : drivers) {
        if (pragmas.inRegion(l.sm().getFullyExpandedLoc(d->getSourceRange().start()))) removed = true;
        else kept.push_back(d);
    }
    return kept;
}

// Flat bit position of index `x` in a packed range (the inverse of declIndex), or nothing when `x` is outside it.
std::optional<uint64_t> flatPosition(const ConstantRange& r, int64_t x, bool packed = true) {
    const int64_t k = packed ? (r.isDescending() ? x - r.lower() : r.upper() - x) : (r.isDescending() ? r.left - x : x - r.left);
    if (k < 0 || k >= static_cast<int64_t>(r.fullWidth())) return std::nullopt;
    return static_cast<uint64_t>(k);
}

// The longest loop whose values a proof enumerates; a longer one stays unproven.
constexpr uint64_t kMaxProvenValues = 1u << 16;

// The assignment targets that a statement writes whenever it runs: those of straight-line code, and those a conditional or a
// case with a default writes (an equivalent target) in every one of its arms.
// With `calls`, a call statement to a user subroutine contributes what the callee's body writes whenever it runs (to a few levels).
void certainWrites(const Statement& s, std::vector<const Expression*>& out, bool calls = false, int depth = 0,
                   std::vector<const Symbol*> blocks = {}) {
    switch (s.kind) {
        case StatementKind::ExpressionStatement: {
            const Expression& e = s.as<ExpressionStatement>().expr;
            if (e.kind == ExpressionKind::Assignment) out.push_back(&e.as<AssignmentExpression>().left());
            else if (calls && depth < 4 && e.kind == ExpressionKind::Call && !e.as<CallExpression>().isSystemCall())
                if (const auto* sub = std::get_if<const SubroutineSymbol*>(&e.as<CallExpression>().subroutine); sub && *sub)
                    certainWrites((*sub)->getBody(), out, true, depth + 1);
            return;
        }
        case StatementKind::Block:
            if (s.as<BlockStatement>().blockKind == StatementBlockKind::Sequential) {
                if (const auto* b = s.as<BlockStatement>().blockSymbol) blocks.push_back(b);
                certainWrites(s.as<BlockStatement>().body, out, calls, depth, blocks);
            }
            return;
        case StatementKind::List:
            // what follows a statement that always leaves the block (return, break, continue, disable of an enclosing block) never runs
            for (auto* c : s.as<StatementList>().list) {
                certainWrites(*c, out, calls, depth, blocks);
                if (alwaysJumps(*c, blocks)) break;
            }
            return;
        case StatementKind::Conditional:
        case StatementKind::Case: {
            std::vector<const Statement*> arms;
            if (s.kind == StatementKind::Conditional) {
                const auto& c = s.as<ConditionalStatement>();
                if (!c.ifFalse) return;
                for (auto& cond : c.conditions)
                    if (cond.pattern) return;
                arms = {&c.ifTrue, c.ifFalse};
            }
            else {
                const auto& c = s.as<CaseStatement>();
                if (!c.defaultCase) return;
                for (auto& g : c.items) arms.push_back(g.stmt);
                arms.push_back(c.defaultCase);
            }
            std::vector<std::vector<const Expression*>> sets(arms.size());
            for (size_t i = 0; i < arms.size(); i++) certainWrites(*arms[i], sets[i], calls, depth, blocks);
            for (auto* e : sets[0]) {
                std::vector<const Expression*> same{e};
                for (size_t i = 1; i < arms.size(); i++) {
                    auto it = std::find_if(sets[i].begin(), sets[i].end(), [&](const Expression* f) { return e->isEquivalentTo(*f); });
                    if (it == sets[i].end()) { same.clear(); break; }
                    same.push_back(*it);
                }
                out.insert(out.end(), same.begin(), same.end());
            }
            return;
        }
        default: return;
    }
}

// An automatic function whose result depends only on its arguments and constants: it declares no static variable, names no
// module state, calls only such functions (no recursion) and no system function outside a pure few.
bool pureFunction(const SubroutineSymbol& sub, std::unordered_set<const SubroutineSymbol*>& seen) {
    if (!seen.insert(&sub).second) return false;
    if (sub.subroutineKind != SubroutineKind::Function || sub.defaultLifetime != VariableLifetime::Automatic) return false;
    struct V : ASTVisitor<V, VisitFlags::AllGood> {
        const SubroutineSymbol& sub;
        std::unordered_set<const SubroutineSymbol*>& seen;
        bool ok = true;
        V(const SubroutineSymbol& s, std::unordered_set<const SubroutineSymbol*>& sn) : sub(s), seen(sn) {}
        bool within(const Symbol& sym) const {
            for (const Scope* sc = sym.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
                if (&sc->asSymbol() == &sub) return true;
            return false;
        }
        void handle(const NamedValueExpression& e) {
            const Symbol& sym = e.symbol;
            if (sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue || sym.kind == SymbolKind::Specparam) return;
            if (!within(sym)) ok = false;
            else if (auto* v = sym.as_if<VariableSymbol>(); v && v->lifetime == VariableLifetime::Static) ok = false;
        }
        void handle(const HierarchicalValueExpression&) { ok = false; }
        void handle(const VariableSymbol& v) {
            if (v.lifetime == VariableLifetime::Static) ok = false;
            visitDefault(v);
        }
        void handle(const CallExpression& c) {
            if (c.isSystemCall()) {
                static const std::unordered_set<std::string_view> pure = {"$clog2", "$bits", "$signed", "$unsigned", "$countones", "$onehot",
                                                                           "$onehot0", "$isunknown", "$size", "$high", "$low", "$left", "$right"};
                if (!pure.count(c.getSubroutineName())) ok = false;
            }
            else {
                const auto* callee = std::get_if<const SubroutineSymbol*>(&c.subroutine);
                if (!callee || !*callee || !pureFunction(**callee, seen)) ok = false;
            }
            visitDefault(c);
        }
    } v(sub, seen);
    sub.getBody().visit(v);
    return v.ok;
}

// Writes through a non-constant index. Slang's driver list omits such writes or keeps them as whole-range drivers, so
// they are collected from the assignments themselves:
//  - module-indexed: the select uses a module-level value (a port or variable). Their bits are not known, so a gap the
//    static drivers leave is Unknown; with no gap there is nothing to report.
//  - loop-indexed: the select uses only block-local values (e.g. loop variables). A write `v[i]` that runs on every
//    iteration of a loop covering every value of `i`, and whose procedure runs it on every pass, drives exactly the bits
//    of those values (provenBits). Any other loop-indexed write is unproven. Slang's driver for such a write is never
//    trusted (it can be the whole range, e.g. for an index with a block-local offset or inside a function), except
//    where slang unrolls a `foreach` and every index is a foreach variable or a constant.
struct DynamicWriteCollector : ConditionalDepthVisitor<DynamicWriteCollector> {
    using ConditionalDepthVisitor<DynamicWriteCollector>::handle;
    Lint& l;
    EvalContext& ec;
    DynWrites out;
    std::vector<LoopValues> proven;   // enclosing loops that run on every iteration, outermost first
    int subroutines = 0;               // enclosing subroutine declarations
    std::unordered_set<const Expression*> certain;   // targets written on every path through the conditional around them
    std::vector<const ValueSymbol*> foreachVars;   // variables of the enclosing foreach loops
    struct PendingWrite { const ValueSymbol* root; Iv bounds; const SubroutineSymbol* routine; };
    std::vector<PendingWrite> pending;   // static writes, judged once the subroutines that are called are known
    std::unordered_set<const SubroutineSymbol*> called;
    struct PendingLive { const ValueSymbol* root; const SubroutineSymbol* routine; std::optional<Iv> bits; };
    std::vector<PendingLive> pendingLive;   // writes in a subroutine body that is not dead code: live once a live call reaches it
    const SubroutineSymbol* routine = nullptr;   // the subroutine being visited
    int dead = 0;        // branches a constant condition excludes
    const Symbol* container = nullptr;   // the procedural block or continuous assignment being visited
    std::unordered_set<const Expression*> initTargets;   // targets of loop initialisers: they run once even in a loop that runs zero times
    int exitLoops = 0;   // loops a certain break, return or disable leaves in a procedure with no `continue`: slang's drivers are exact there
    std::vector<const Symbol*> blocks;   // the named blocks around the statement being visited
    void handle(const BlockStatement& b) {
        if (b.blockSymbol) blocks.push_back(b.blockSymbol);
        this->visitDefault(b);
        if (b.blockSymbol) blocks.pop_back();
    }
    // statements after a certain break, continue, return or disable never run
    void handle(const StatementList& s) {
        int after = 0, mixedAfter = 0;
        for (auto* c : s.list) {
            c->visit(*this);
            if (after) continue;
            if (alwaysJumps(*c, blocks)) { ++dead; after = 1; continue; }
            // `if (!en) disable blk;` with `en` tied: the statements after it run when the tie says they do
            if (c->kind != StatementKind::Conditional) continue;
            const auto& cs = c->as<ConditionalStatement>();
            if (cs.conditions.size() != 1 || cs.conditions[0].pattern) {
                const bool jumps = alwaysJumps(cs.ifTrue, blocks) || (cs.ifFalse && alwaysJumps(*cs.ifFalse, blocks));
                tieMixed = false;
                for (const auto& condition : cs.conditions)
                    if (readsTie(*condition.expr)) tieMixed = true;
                if (jumps && std::exchange(tieMixed, false)) { ++mixedGuard; ++mixedAfter; }
                continue;
            }
            const bool trueJumps = alwaysJumps(cs.ifTrue, blocks), falseJumps = cs.ifFalse && alwaysJumps(*cs.ifFalse, blocks);
            if (!trueJumps && !falseJumps) continue;
            tieMixed = false;
            const auto value = callsSubroutine(*cs.conditions[0].expr) ? std::nullopt : decided(*cs.conditions[0].expr);
            const bool mixed = std::exchange(tieMixed, false);
            if (callsSubroutine(*cs.conditions[0].expr) && readsTie(*cs.conditions[0].expr)) {
                ++mixedGuard; ++mixedAfter;
            } else if (value) {
                if (*value ? trueJumps : falseJumps) { ++dead; after = 1; }
            } else if (mixed) { ++mixedGuard; ++mixedAfter; }
        }
        dead -= after;
        mixedGuard -= mixedAfter;
    }
    int unknownLoops = 0;   // enclosing loops slang did not unroll (no constant start, bound and step; while, do, repeat, forever, foreach): the exit exclusion does not reach their writes
    bool procContinue = false;   // the procedure being visited has a `continue` somewhere
    const std::unordered_set<const ValueSymbol*>& temps;   // module-level procedure temporaries (analysis/temps.h)
    DynamicWriteCollector(Lint& lint, EvalContext& e, const std::unordered_set<const ValueSymbol*>& t) : l(lint), ec(e), temps(t) {}

    void noteLhs(const Expression& lhs) {
        ValuePath::visitPaths(
            lhs, ec,
            [&](const ValuePath& p) {
                auto* root = p.rootSymbol();
                if (root && mixedGuard != 0) out.maybeOff.insert(root);
                if (root && unwrapImplicit(lhs).kind == ExpressionKind::NamedValue && mixedGuard == 0 && dead == 0 && guarded == 0 && unknownLoops == 0 && !routine && inProcedure) out.alwaysWritten.insert(root);
                if (root && (dead == 0 || initTargets.count(&lhs))) {
                    // the bits of a live write: a whole-object write is the whole range, a static select its bounds; an index not constant
                    // after unrolling is placed by the other proofs (the symbol is then dynamic, never decided from here)
                    std::optional<Iv> bits;
                    if (p.empty()) bits = Iv{0, std::max<uint64_t>(root->getType().getSelectableWidth(), 1) - 1};
                    else if (p.isFullyStatic()) bits = p.lspBounds;
                    if (routine) pendingLive.push_back({root, routine, bits});
                    else { out.liveWrites.insert(root); if (bits) out.liveBits[root].push_back(*bits); }
                }
                if (root && dead != 0 && !initTargets.count(&lhs)) { out.deadWrites.insert(root); out.deadSites.emplace_back(p.rootExpr->sourceRange.start(), container); }
                if (root && !p.empty() && p.isFullyStatic() && dead == 0 && (exitLoops == 0 || unknownLoops != 0)) pending.push_back({root, p.lspBounds, routine});
                if (!root || p.empty() || p.isFullyStatic()) return;
                // after a certain jump the write never runs: only slang's driver for it is set aside
                if (dead != 0) { out.dynamicWriteRoots.push_back(p.rootExpr->sourceRange.start()); return; }
                SelectorRefs refs;
                refs.root = root;
                refs.proven = &proven;
                refs.temps = &temps;
                p.fullExpr->visit(refs);
                if (refs.found) {
                    out.moduleIdx.insert(root);
                    out.dynamicWriteRoots.push_back(p.rootExpr->sourceRange.start());
                    if (!placeByRange(*p.fullExpr, root)) out.moduleUndecided.insert(root);
                    return;
                }
                if (unrolledForeachWrite(lhs)) return;
                out.loopIdx.insert(root);
                out.dynamicWriteRoots.push_back(p.rootExpr->sourceRange.start());
                if (subroutines != 0) out.subroutineDynamicWrites.insert(root);
                if (!provenWrite(*p.fullExpr, lhs, root)) out.unprovenLoop.insert(root);
            },
            /* skipSelectors */ true);
    }
    // Every symbol read by a condition of the procedure being visited (an `if`, case selector or item, `?:`, loop test, short-circuit
    // operand); `condCalls` when one of them calls a user subroutine, whose reads are not listed.
    std::unordered_set<const ValueSymbol*> condSyms;
    bool condCalls = false;
    void collectConditions(const Statement& body) {
        struct Syms : ASTVisitor<Syms, VisitFlags::AllGood> {
            DynamicWriteCollector& c;
            explicit Syms(DynamicWriteCollector& x) : c(x) {}
            void handle(const NamedValueExpression& e) { c.condSyms.insert(&e.symbol); }
            void handle(const HierarchicalValueExpression& e) { c.condSyms.insert(&e.symbol); c.condCalls = true; }
            void handle(const CallExpression& e) { if (!e.isSystemCall()) c.condCalls = true; visitDefault(e); }
        };
        struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
            Syms syms;
            explicit Find(DynamicWriteCollector& x) : syms(x) {}
            void take(const Expression& e) { e.visit(syms); }
            void handle(const ConditionalStatement& s) {
                for (const auto& c : s.conditions) { take(*c.expr); if (c.pattern) syms.c.condCalls = true; }
                visitDefault(s);
            }
            void handle(const CaseStatement& s) {
                take(s.expr);
                for (const auto& item : s.items) for (auto* e : item.expressions) take(*e);
                visitDefault(s);
            }
            void handle(const PatternCaseStatement& s) { syms.c.condCalls = true; visitDefault(s); }
            void handle(const ConditionalExpression& e) {
                for (const auto& c : e.conditions) { take(*c.expr); if (c.pattern) syms.c.condCalls = true; }
                visitDefault(e);
            }
            void handle(const BinaryExpression& e) {
                if (e.op == BinaryOperator::LogicalAnd || e.op == BinaryOperator::LogicalOr || e.op == BinaryOperator::LogicalImplication) {
                    take(e.left());
                    take(e.right());
                }
                visitDefault(e);
            }
            void handle(const WhileLoopStatement& s) { take(s.cond); visitDefault(s); }
            void handle(const DoWhileLoopStatement& s) { take(s.cond); visitDefault(s); }
            void handle(const ForLoopStatement& s) { if (s.stopExpr) take(*s.stopExpr); visitDefault(s); }
            void handle(const RepeatLoopStatement& s) { take(s.count); visitDefault(s); }
        } find(*this);
        body.visit(find);
    }
    // True when `sym` is one of `from`, or is driven (through up to a few levels of assignments) by an expression that reads one of them.
    bool derivedFrom(const ValueSymbol& sym, const std::unordered_set<const ValueSymbol*>& from, int depth, std::unordered_set<const ValueSymbol*>& seen) {
        if (from.count(&sym)) return true;
        if (!seen.insert(&sym).second) return false;
        auto* am = analysisOf(l);
        if (!am) return true;
        for (auto* d : am->getDrivers(sym)) {
            if (depth == 0 && !d->isInputPort()) return true;   // beyond the trace limit: not shown to be independent
            std::unordered_set<const ValueSymbol*> reads;
            struct Reads : ASTVisitor<Reads, VisitFlags::AllGood> {
                std::unordered_set<const ValueSymbol*>& out; bool unknown = false;
                explicit Reads(std::unordered_set<const ValueSymbol*>& o) : out(o) {}
                void handle(const NamedValueExpression& e) { out.insert(&e.symbol); }
                void handle(const HierarchicalValueExpression&) { unknown = true; }
                void handle(const CallExpression& e) { if (!e.isSystemCall()) unknown = true; visitDefault(e); }
            } r(reads);
            if (d->isInputPort()) continue;   // set from outside this module
            const Symbol* cs = d->containingSymbol;
            if (!cs) return true;
            if (cs->kind == SymbolKind::ContinuousAssign) cs->as<ContinuousAssignSymbol>().getAssignment().visit(r);
            else if (cs->kind == SymbolKind::ProceduralBlock) cs->as<ProceduralBlockSymbol>().getBody().visit(r);
            else return true;
            if (r.unknown) return true;
            for (auto* x : reads)
                if (x != &sym && derivedFrom(*x, from, depth - 1, seen)) return true;
        }
        return false;
    }
    // A write through a design value of a known range: the bits it can reach are those of the elements that range names, whatever
    // the value is. Decided only where nothing narrows the range below its type: a condition of the procedure that reads the index,
    // or anything derived from it, may exclude values, and then which members are driven is not known.
    bool placeByRange(const Expression& target, const ValueSymbol* root) {
        if (subroutines != 0 || (inProcedure && condCalls)) return false;
        return placeRange(target, root, inProcedure);
    }
    bool inProcedure = false;
    bool placeRange(const Expression& target, const ValueSymbol* root, bool conditions) {
        std::vector<LoopValues> scope;
        std::unordered_set<const ValueSymbol*> selector;
        bool ok = true;
        bool narrowedInputs = false;   // an input whose actual reaches some of the values of its type only: which of them it takes is not known
        target.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
            if (&sym == root || sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue || sym.kind == SymbolKind::Specparam ||
                sym.kind == SymbolKind::Field)
                return;
            if (!sym.isValue()) { ok = false; return; }
            const auto& v = sym.as<ValueSymbol>();
            if (!isModuleValue(v)) { ok = false; return; }
            if (selector.count(&v)) return;
            auto range = typeRange(v.getType());
            if (!range || range->hi - range->lo + 1 > static_cast<Wide>(kMaxProvenValues)) { ok = false; return; }
            // An input port takes what this instance connects to it: a constant, or an expression that reaches fewer values than the type.
            if (isInputPort(v))
            {
                bool differ = false, untraced = false, inexact = false;
                const auto actual = instanceRange(v, 0, differ, untraced, inexact);
                if (untraced) narrowedInputs = true;   // an actual that is neither a constant nor traced to a top-level input: which values it takes is not known
                if (!actual) narrowedInputs = true;   // an actual that cannot be read: the port is not known to take every value
                else if (actual->within(*range)) {
                    range = *actual;
                    if (inexact || differ) narrowedInputs = true;
                }
            }
            selector.insert(&v);
            LoopValues lv;
            lv.var = &v;
            lv.first = static_cast<int64_t>(range->lo);
            lv.step = 1;
            lv.count = static_cast<uint64_t>(range->hi - range->lo + 1);
            scope.push_back(lv);
        });
        if (!ok || scope.empty()) return false;
        // Every value of the index's type is taken only by an input port nothing in this module narrows: any other source (a wire or a
        // register fed by something narrower, a mask, a counter) may take a subset.
        bool freeInputs = !narrowedInputs;
        for (auto* v : selector) freeInputs = freeInputs && isInputPort(*v);
        if (conditions) {
            for (auto* c : condSyms) {
                std::unordered_set<const ValueSymbol*> seen;
                if (derivedFrom(*c, selector, 4, seen)) return false;
            }
        }
        std::vector<Iv> bits;
        if (!placeBits(target, root, scope, bits)) return false;
        auto& dst = out.rangeBits[root];
        dst.insert(dst.end(), bits.begin(), bits.end());
        if (!freeInputs) out.rangeUnproven.insert(root);
        return true;
    }
    // The instances that share a canonical body (the rules visit that body once for all of them), found once over the design.
    std::unordered_map<const InstanceBodySymbol*, std::vector<const InstanceSymbol*>> sharing;
    bool sharingBuilt = false;
    const std::vector<const InstanceSymbol*>& instancesOf(const InstanceBodySymbol& body) {
        if (!sharingBuilt) {
            sharingBuilt = true;
            struct All : ASTVisitor<All, VisitFlags::Symbols> {
                std::unordered_map<const InstanceBodySymbol*, std::vector<const InstanceSymbol*>>& into;
                explicit All(decltype(into) m) : into(m) {}
                void handle(const InstanceSymbol& inst) {
                    into[inst.getCanonicalBody() ? inst.getCanonicalBody() : &inst.body].push_back(&inst);
                    visitDefault(inst);
                }
            } all(sharing);
            l.compilation().getRoot().visit(all);
        }
        static const std::vector<const InstanceSymbol*> none;
        const auto it = sharing.find(&body);
        return it == sharing.end() ? none : it->second;
    }
    // The values the port `v` can take from what each instance of its module connects to it (their hull): the whole range of its type
    // for a port of the top module or one fed by a free input of the parent, a constant, or the range of the connected expression.
    // Nothing when a port is not connected or a connection cannot be followed; `untraced` is set when an actual is neither a constant nor
    // traced through plain port connections to a top-level input.
    std::optional<Interval> instanceRange(const ValueSymbol& v, int depth, bool& differ, bool& untraced, bool& inexact) {
        const auto* body = enclosingBody(v);
        const auto* port = body ? body->findPort(v.name) : nullptr;
        if (!port || port->kind != SymbolKind::Port || port->as<PortSymbol>().internalSymbol != &v) return std::nullopt;
        const auto full = typeRange(v.getType());
        if (!full) return std::nullopt;
        const auto& instances = instancesOf(*body);
        if (instances.empty()) return std::nullopt;
        std::optional<Interval> hull;
        for (const auto* inst : instances) {
            std::optional<Interval> r;
            if (inst->isTopLevel()) r = full;
            for (const auto* pc : inst->getPortConnections()) {
                if (r || !pc || pc->port.name != port->name) continue;
                const Expression* actual = pc->getExpression();
                if (!actual) return std::nullopt;
                const Expression* x = actual;
                while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
                if (x->kind == ExpressionKind::NamedValue && depth < 8)
                    if (const auto* up = x->as<NamedValueExpression>().symbol.as_if<ValueSymbol>(); up && isInputPort(*up) && x->type->getBitWidth() == v.getType().getBitWidth()) {
                        r = instanceRange(*up, depth + 1, differ, untraced, inexact);
                        if (!r) return std::nullopt;
                        continue;
                    }
                if (const auto freeWidth = freeActualWidth(*x, depth)) {
                    // every bit of the actual is a distinct bit of a free top-level input, possibly through a bijection: the port takes every
                    // value of those bits (zero-extended into a wider port), exactly
                    const unsigned w = std::min<uint64_t>(*freeWidth, v.getType().getBitWidth());
                    r = w >= 63 ? *full : Interval{0, (Wide{1} << w) - 1};
                    if (!(r->within(*full))) r = *full;
                    if (hull && !(*hull == *r)) differ = true;
                    hull = hull ? Interval{std::min(hull->lo, r->lo), std::max(hull->hi, r->hi)} : *r;
                    continue;
                }
                ValueRanges ranges(l);
                const auto value = ranges.of(*actual);
                r = value && value->within(*full) ? *value : *full;
                if (value && value->within(*full) && value->lo != value->hi && !(*value == *full)) inexact = true;
                // Only a constant, or a range narrower than the type, says which values the port takes; the full range of a register, a
                // procedural net, another child's output or an expression is its type's, not a set of values it is known to reach.
                if (!(value && value->within(*full) && (value->lo == value->hi || !(*value == *full)))) untraced = true;
            }
            if (!r) return std::nullopt;
            if (hull && !(*hull == *r)) differ = true;   // the instances do not agree: what one of them reaches the shared body cannot say
            hull = hull ? Interval{std::min(hull->lo, r->lo), std::max(hull->hi, r->hi)} : *r;
        }
        return hull;
    }
    // The width of `e` when every bit of it is a distinct bit of a free top-level input (a port of the top module, or one fed by one
    // through plain connections that every instance agrees on), reached by a constant slice or bit select, a plain alias net, a
    // concatenation, a mux whose arms are such, or an XOR with a constant: then it takes every value of its width, and an unsigned
    // port it feeds takes every value of the bits it keeps. Nothing for any other shape.
    std::optional<uint64_t> freeActualWidth(const Expression& e0, int depth) {
        if (e0.type->isSigned() || !e0.type->isIntegral()) return std::nullopt;
        std::set<std::pair<const Symbol*, int64_t>> bits;
        uint64_t n = 0;
        if (!freeBits(e0, depth, bits, 0, n)) return std::nullopt;
        if (bits.empty() || n == 0) return std::nullopt;
        return n;   // the free bits the value is made of, not the width the context gave the expression: the rest is zero
    }
    // `n` is the number of low bits of `e0` that are free (every combination of them is reached, the bits above are zero).
    bool freeBits(const Expression& e0, int depth, std::set<std::pair<const Symbol*, int64_t>>& bits, int nest, uint64_t& n) {
        if (nest > 8 || depth > 8) return false;
        const Expression& e = unwrapImplicit(e0);
        auto add = [&](const Symbol* sym, int64_t k) { return bits.insert({sym, k}).second; };
        switch (e.kind) {
            case ExpressionKind::NamedValue: {
                const auto* sym = e.as<NamedValueExpression>().symbol.as_if<ValueSymbol>();
                if (!sym || !e.type->isIntegral()) return false;
                if (isInputPort(*sym)) {
                    bool differ = false, untraced = false, inexact = false;
                    const auto r = instanceRange(*sym, depth + 1, differ, untraced, inexact);
                    const auto full = typeRange(sym->getType());
                    if (!r || !full || differ || untraced || inexact || !(*r == *full)) return false;
                    for (int64_t k = 0; k < static_cast<int64_t>(e.type->getBitWidth()); k++)
                        if (!add(sym, k)) return false;
                    n = e.type->getBitWidth();
                    return true;
                }
                if (sym->kind != SymbolKind::Net || sym->getType().isSigned()) return false;
                const auto* am = analysisOf(l);
                if (!am) return false;
                const auto drivers = am->getDrivers(*sym);
                const Expression* rhsp = nullptr;
                if (drivers.empty()) rhsp = sym->getInitializer();   // a net declaration assignment (`wire w = x;`) is not listed as a driver
                else if (drivers.size() == 1 && drivers[0]->kind == DriverKind::Continuous && !drivers[0]->isInputPort() && drivers[0]->containingSymbol &&
                         drivers[0]->containingSymbol->kind == SymbolKind::ContinuousAssign && !sym->getInitializer()) {
                    const auto& dd = *drivers[0];
                    if (dd.getBounds().first != 0 || dd.getBounds().second + 1 != sym->getType().getBitWidth()) return false;
                    const Expression& a = dd.containingSymbol->as<ContinuousAssignSymbol>().getAssignment();
                    if (a.kind != ExpressionKind::Assignment) return false;
                    rhsp = &a.as<AssignmentExpression>().right();
                }
                if (!rhsp) return false;
                const Expression& rhs = unwrapImplicit(*rhsp);
                if (rhs.kind != ExpressionKind::NamedValue || rhs.type->isSigned() || rhs.type->getBitWidth() != sym->getType().getBitWidth()) return false;
                std::set<std::pair<const Symbol*, int64_t>> inner;
                if (!freeBits(rhs, depth + 1, inner, nest + 1, n)) return false;
                for (const auto& b : inner)
                    if (!bits.insert(b).second) return false;
                return true;
            }
            case ExpressionKind::ElementSelect:
            case ExpressionKind::RangeSelect: {
                const bool range = e.kind == ExpressionKind::RangeSelect;
                const Expression& base = range ? e.as<RangeSelectExpression>().value() : e.as<ElementSelectExpression>().value();
                const Type& bt = *base.type;
                if (unwrapImplicit(base).kind != ExpressionKind::NamedValue) return false;   // a plain symbol: its bit offsets are the leaves' own
                if (!bt.isIntegral() || bt.isSigned() || !bt.hasFixedRange() || bt.getFixedRange().width() != bt.getBitWidth()) return false;
                int64_t a, b;
                if (range) {
                    const auto& rs = e.as<RangeSelectExpression>();
                    if (rs.getSelectionKind() != RangeSelectionKind::Simple) return false;
                    const auto lv = l.evalConst(rs.left()), rv = l.evalConst(rs.right());
                    if (!lv.isInteger() || !rv.isInteger() || lv.integer().hasUnknown() || rv.integer().hasUnknown()) return false;
                    a = *lv.integer().as<int64_t>();
                    b = *rv.integer().as<int64_t>();
                }
                else {
                    const auto iv = l.evalConst(e.as<ElementSelectExpression>().selector());
                    if (!iv.isInteger() || iv.integer().hasUnknown()) return false;
                    a = b = *iv.integer().as<int64_t>();
                }
                const auto fr = bt.getFixedRange();
                if (!fr.containsPoint(static_cast<int32_t>(a)) || !fr.containsPoint(static_cast<int32_t>(b))) return false;
                const int64_t ta = fr.translateIndex(static_cast<int32_t>(a)), tb = fr.translateIndex(static_cast<int32_t>(b));
                std::set<std::pair<const Symbol*, int64_t>> inner;
                uint64_t baseN = 0;
                if (!freeBits(base, depth, inner, nest + 1, baseN)) return false;
                (void)baseN;
                std::vector<std::pair<const Symbol*, int64_t>> all(inner.begin(), inner.end());
                // the base's bits in offset order: only a single leaf symbol's contiguous bits can be sliced by offset
                if (inner.empty()) return false;
                const Symbol* sym = all.front().first;
                for (const auto& x : all) if (x.first != sym) return false;
                for (int64_t k = std::min(ta, tb); k <= std::max(ta, tb); k++) {
                    if (!inner.count({sym, k})) return false;
                    if (!add(sym, k)) return false;
                }
                n = static_cast<uint64_t>(std::max(ta, tb) - std::min(ta, tb) + 1);
                return true;
            }
            case ExpressionKind::Concatenation: {
                n = 0;
                for (const auto* op : e.as<ConcatenationExpression>().operands()) {
                    std::set<std::pair<const Symbol*, int64_t>> inner;
                    uint64_t opN = 0;
                    if (!op->type->isIntegral() || !freeBits(*op, depth, inner, nest + 1, opN)) return false;
                    if (opN != op->type->getBitWidth()) return false;   // an operand that is only partly free: the bits between are constant
                    for (const auto& b : inner)
                        if (!add(b.first, b.second)) return false;
                    n += opN;
                }
                return true;
            }
            case ExpressionKind::ConditionalOp: {
                const auto& c = e.as<ConditionalExpression>();
                if (c.conditions.size() != 1 || c.conditions[0].pattern) return false;
                std::set<std::pair<const Symbol*, int64_t>> x, y;
                uint64_t xn = 0, yn = 0;
                if (!freeBits(c.left(), depth, x, nest + 1, xn) || !freeBits(c.right(), depth, y, nest + 1, yn)) return false;
                for (const auto& b : x) if (!add(b.first, b.second)) return false;
                for (const auto& b : y) if (!add(b.first, b.second)) return false;
                // arms narrower than the context are zero-extended: the value takes the low words the arms' free bits name
                n = xn;
                return xn == yn && x.size() == y.size();
            }
            case ExpressionKind::BinaryOp: {
                const auto& b = e.as<BinaryExpression>();
                if (b.op != BinaryOperator::BinaryXor) return false;
                auto isConst = [&](const Expression& x) { const auto cv = l.evalConst(x); return cv.isInteger() && !cv.integer().hasUnknown(); };
                const Expression* var = isConst(b.right()) ? &b.left() : isConst(b.left()) ? &b.right() : nullptr;
                if (!var) return false;
                std::set<std::pair<const Symbol*, int64_t>> inner;
                uint64_t varN = 0;
                if (!freeBits(*var, depth, inner, nest + 1, varN)) return false;
                // the constant's bits above the free ones are set or cleared: the value is a bijection of the free bits only when they fill the whole width
                if (varN != e.type->getBitWidth()) return false;
                for (const auto& x : inner) if (!add(x.first, x.second)) return false;
                n = varN;
                return true;
            }
            default: return false;
        }
    }
    // What the instances of the module that owns input port `v` connect to it. `value` is the one constant every instance ties it to
    // (a 1-bit port also has it from `truth`); `truth` whether that constant is nonzero, when the instances agree on that only;
    // `any` when some instance connects a constant to it, wholly or in part (a concatenation with a constant operand, a mask with a
    // constant, an X or Z), so a decision that reads it is not the same for every instance unless `value` or `truth` says so.
    struct Tie {
        std::optional<bool> truth;
        std::optional<SVInt> value;
        bool any = false;
    };
    std::map<const ValueSymbol*, Tie> tieCache;
    using Binds = std::vector<std::pair<const ValueSymbol*, SVInt>>;
    // `e` evaluated with the given variables bound to constants; nothing when it still reads anything else that is not constant.
    std::optional<SVInt> evalBound(const Expression& e, const Binds& binds) {
        ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
        EvalContext tc(ac);
        tc.pushEmptyFrame();
        for (const auto& b : binds) tc.createLocal(b.first, ConstantValue(b.second));
        const auto cv = e.eval(tc);
        if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
        return cv.integer();
    }
    // As evalBound, and an AND with an operand that is all zeros, an OR with an operand that is all ones, settle whatever the other operand is.
    std::optional<SVInt> evalPartial(const Expression& e0, const Binds& binds, int depth = 0) {
        if (auto v = evalBound(e0, binds)) return v;
        const Expression& e = unwrapImplicit(e0);
        if (depth > 6 || e.kind != ExpressionKind::BinaryOp || !e.type->isIntegral()) return std::nullopt;
        const auto& b = e.as<BinaryExpression>();
        const bool isAnd = b.op == BinaryOperator::BinaryAnd || b.op == BinaryOperator::LogicalAnd;
        const bool isOr = b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::LogicalOr;
        if (!isAnd && !isOr) return std::nullopt;
        const auto width = e.type->getBitWidth();
        for (const Expression* x : {&b.left(), &b.right()}) {
            const auto v = evalPartial(*x, binds, depth + 1);
            if (!v || v->getBitWidth() != width) continue;
            if (isAnd && *v == 0) return SVInt(width, 0, e.type->isSigned());
            if (isOr && bool(~*v == 0)) return *v;
        }
        return std::nullopt;
    }
    // True for a connection that is partly a constant: a concatenation operand, an operand of a bitwise or logical operator, an arm of `?:`.
    bool partlyConstant(const Expression& e) {
        struct Part : ASTVisitor<Part, VisitFlags::AllGood> {
            Lint& l;
            bool found = false;
            explicit Part(Lint& lint) : l(lint) {}
            bool constant(const Expression& x) { return l.evalConst(unwrapImplicit(x)).isInteger(); }
            void handle(const ConcatenationExpression& c) {
                for (const auto* op : c.operands()) { if (constant(*op)) found = true; else op->visit(*this); }
            }
            void handle(const ConditionalExpression& c) {
                for (const auto* arm : {&c.left(), &c.right()}) { if (constant(*arm)) found = true; else arm->visit(*this); }
            }
            void handle(const BinaryExpression& b) {
                switch (b.op) {
                    case BinaryOperator::BinaryAnd: case BinaryOperator::BinaryOr: case BinaryOperator::BinaryXor: case BinaryOperator::BinaryXnor:
                    case BinaryOperator::LogicalAnd: case BinaryOperator::LogicalOr:
                        if (constant(b.left()) || constant(b.right())) found = true;
                        break;
                    default: break;
                }
                visitDefault(b);
            }
        } part(l);
        e.visit(part);
        return part.found;
    }
    Tie tieOf(const ValueSymbol& v, int depth) {
        if (const auto it = tieCache.find(&v); it != tieCache.end()) return it->second;
        const Tie t = computeTie(v, depth);
        if (depth == 0) tieCache[&v] = t;
        return t;
    }
    Tie computeTie(const ValueSymbol& v, int depth) {
        const auto* body = enclosingBody(v);
        const auto* port = body ? body->findPort(v.name) : nullptr;
        if (!port || depth > 8) return {};
        const auto& instances = instancesOf(*body);
        if (instances.empty()) return {};
        const uint64_t width = v.getType().getBitWidth();
        std::optional<bool> all;
        std::optional<SVInt> value;
        bool agree = true, agreeValue = true, any = false;
        for (const auto* inst : instances) {
            std::optional<bool> one;
            std::optional<SVInt> oneValue;
            bool partial = false;
            if (!inst->isTopLevel()) {
                bool seen = false;
                for (const auto* pc : inst->getPortConnections()) {
                    if (!pc || pc->port.name != port->name) continue;
                    seen = true;
                    const Expression* actual = pc->getExpression();
                    if (!actual) break;
                    const Expression* x = actual;
                    while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
                    auto cv = l.evalConst(*actual);
                    if (cv.isInteger() && !cv.integer().hasUnknown()) {
                        one = !bool(cv.integer() == 0);
                        oneValue = cv.integer();
                        break;
                    }
                    // what the parent's own ties and the nets it derives from them give the connection (a constant net, an input the parent ties)
                    Binds up;
                    bool upRead = false;
                    bindTies(*actual, up, upRead, 0);
                    if (!up.empty()) oneValue = evalPartial(*actual, up);
                    if (oneValue) one = !bool(*oneValue == 0);
                    else if (x->kind == ExpressionKind::NamedValue) {
                        // the parent's inputs differ in value but agree in truth: that survives a connection that keeps all its bits
                        const auto* sym = x->as<NamedValueExpression>().symbol.as_if<ValueSymbol>();
                        if (sym && isInputPort(*sym))
                            if (const Tie ut = tieOf(*sym, depth + 1); ut.truth && actual->type->getBitWidth() >= sym->getType().getBitWidth()) one = ut.truth;
                    }
                    // an X or Z connection, an aggregate constant, a constant in part of it, or a tie above that is not decided here
                    if (!one) partial = cv.isInteger() || !cv.bad() || upRead || partlyConstant(*actual);
                    break;
                }
                if (!seen) { one.reset(); oneValue.reset(); }
            }
            if (oneValue) {
                if (oneValue->getBitWidth() != width) oneValue.reset();
                else oneValue->setSigned(v.getType().isSigned());
            }
            if (one || partial) any = true;
            if (!one || (all && *all != *one)) agree = false;
            if (one && !all) all = one;
            if (!oneValue || (value && !exactlyEqual(*value, *oneValue))) agreeValue = false;
            if (oneValue && !value) value = oneValue;
        }
        Tie t;
        t.any = any;
        if (agree) t.truth = all;
        if (agreeValue) t.value = value;
        if (!t.value && t.truth && width == 1) t.value = SVInt(1, *t.truth ? 1 : 0, false);
        return t;
    }
    // Whether the input port `v` is nonzero, when every instance of its module ties it to a constant of the same truth value (through
    // plain port connections of input ports); nothing when it is free, unconnected or the instances disagree.
    std::optional<bool> tiedPort(const ValueSymbol& v, int depth) {
        const Tie t = tieOf(v, depth);
        if (t.any && !t.truth) tieMixed = true;   // some instances tie it to a constant and others do not (or to the other one)
        return t.truth;
    }
    // The variables `e` reads whose value the ties decide: input ports every instance ties to one constant, and nets they alone drive
    // through one continuous assignment of such values. `tieRead` is set when `e` reads a port some instance ties, even if the value
    // is not decided (some instances differ, or the tie is in part of the connection, or the net also reads a free signal).
    void bindTies(const Expression& e, Binds& binds, bool& tieRead, int depth) {
        struct Reads : ASTVisitor<Reads, VisitFlags::AllGood> {
            std::vector<const ValueSymbol*> syms;
            void handle(const NamedValueExpression& n) { syms.push_back(&n.symbol); }
        } reads;
        e.visit(reads);
        for (const auto* sym : reads.syms) {
            if (std::any_of(binds.begin(), binds.end(), [&](const auto& b) { return b.first == sym; })) continue;
            if (isInputPort(*sym)) {
                const Tie t = tieOf(*sym, 0);
                if (t.any) tieRead = true;
                if (t.value) binds.emplace_back(sym, *t.value);
                continue;
            }
            if (depth >= 4 || sym->kind != SymbolKind::Net || !sym->getType().isIntegral()) continue;
            const auto* am = analysisOf(l);
            if (!am) continue;
            const auto drivers = am->getDrivers(*sym);
            const Expression* rhs = nullptr;
            if (drivers.empty()) rhs = sym->getInitializer();
            else if (drivers.size() == 1 && drivers[0]->kind == DriverKind::Continuous && !drivers[0]->isInputPort() && drivers[0]->containingSymbol &&
                     drivers[0]->containingSymbol->kind == SymbolKind::ContinuousAssign && !sym->getInitializer() &&
                     drivers[0]->getBounds().first == 0 && drivers[0]->getBounds().second + 1 == sym->getType().getBitWidth()) {
                const Expression& a = drivers[0]->containingSymbol->as<ContinuousAssignSymbol>().getAssignment();
                if (a.kind == ExpressionKind::Assignment) rhs = &a.as<AssignmentExpression>().right();
            }
            if (!rhs) continue;
            Binds inner;
            bool innerRead = false;
            bindTies(*rhs, inner, innerRead, depth + 1);
            if (innerRead) tieRead = true;
            if (auto v = evalPartial(*rhs, inner); v && v->getBitWidth() == sym->getType().getBitWidth()) {
                v->setSigned(sym->getType().isSigned());   // the net's own signedness decides how a condition reads it
                binds.emplace_back(sym, *v);
            }
        }
    }
    // Whether `e` reads something a tie decides in some instance or in part: a condition that reads it and is not decided is not "live",
    // it is unknown.
    bool readsTie(const Expression& e) {
        Binds binds;
        bool tieRead = false;
        bindTies(e, binds, tieRead, 0);
        return tieRead;
    }
    // The value of `e` when the ties decide it.
    std::optional<SVInt> evalTied(const Expression& e) {
        if (callsSubroutine(e)) return std::nullopt;
        Binds binds;
        bool tieRead = false;
        bindTies(e, binds, tieRead, 0);
        if (binds.empty()) return std::nullopt;
        return evalPartial(e, binds);
    }
    bool tieMixed = false;   // set by tiedPort when instances disagree on a constant
    int mixedGuard = 0;      // enclosing conditions that tieMixed made undecided
    static bool isInputPort(const ValueSymbol& v) {
        const auto* body = enclosingBody(v);
        const auto* port = body ? body->findPort(v.name) : nullptr;
        return port && port->kind == SymbolKind::Port && port->as<PortSymbol>().direction == ArgumentDirection::In &&
               port->as<PortSymbol>().internalSymbol == &v;
    }
    // A target whose every index is a foreach variable or a constant: slang unrolls the loop and drives exactly those elements.
    bool unrolledForeachWrite(const Expression& lhs) const {
        if (foreachVars.empty()) return false;
        const Expression* x = &unwrapImplicit(lhs);
        bool any = false;
        for (;;) {
            if (x->kind == ExpressionKind::ElementSelect) {
                const auto& es = x->as<ElementSelectExpression>();
                const Expression& sel = unwrapImplicit(es.selector());
                if (sel.kind == ExpressionKind::NamedValue) {
                    if (std::find(foreachVars.begin(), foreachVars.end(), &sel.as<NamedValueExpression>().symbol) == foreachVars.end()) return false;
                    any = true;
                } else if (!constantInt(l, sel)) return false;
                x = &unwrapImplicit(es.value());
            } else if (x->kind == ExpressionKind::MemberAccess) x = &unwrapImplicit(x->as<MemberAccessExpression>().value());
            else break;
        }
        return any && x->kind == ExpressionKind::NamedValue;
    }
    // A target made of constant or proven-loop indices and member names: `v[e]`, `v[e +: W]`, `a[c][e]`, `s.m[e].f`, over vectors,
    // packed and unpacked arrays and structs. Every index `e` is built from the variables of proven loops, constants and
    // parameters only (`i`, `i + c`, `k * 2 + 1`, `32 * i`), so evaluating it per value (nested loops combine) gives the exact
    // bits the write names. Nothing above the write may be conditional, unless every path through that condition makes the same
    // write (`certain`). Records the bits of every combination of the loops' values.
    bool provenWrite(const Expression& target, const Expression& stmtLhs, const ValueSymbol* root) {
        if (guarded != 0 && certain.count(&stmtLhs) == 0) return false;
        std::vector<Iv> bits;
        if (!placeBits(target, root, proven, bits)) return false;
        auto& dst = out.provenBits[root];
        dst.insert(dst.end(), bits.begin(), bits.end());
        return true;
    }
    // The bits `target` can name when each variable its indices read takes the values of its entry in `scope` (independently of the
    // others): exact for the loop variables of a proven loop, a superset for a design value of a known range. False when the target is
    // not of the supported shape or too many combinations would have to be tried.
    bool placeBits(const Expression& target, const ValueSymbol* root, const std::vector<LoopValues>& scope, std::vector<Iv>& placed) {
        // The chain, outermost select first; only the outermost may be a part select.
        std::vector<const Expression*> steps;
        const Expression* x = &unwrapImplicit(target);
        for (;;) {
            if (x->kind == ExpressionKind::ElementSelect) {
                steps.push_back(x);
                x = &unwrapImplicit(x->as<ElementSelectExpression>().value());
            }
            else if (x->kind == ExpressionKind::MemberAccess) {
                steps.push_back(x);
                x = &unwrapImplicit(x->as<MemberAccessExpression>().value());
            }
            else if (x->kind == ExpressionKind::RangeSelect && steps.empty() &&
                     x->as<RangeSelectExpression>().getSelectionKind() != RangeSelectionKind::Simple) {
                steps.push_back(x);
                x = &unwrapImplicit(x->as<RangeSelectExpression>().value());
            }
            else break;
        }
        if (x->kind != ExpressionKind::NamedValue || &x->as<NamedValueExpression>().symbol != root || steps.empty()) return false;
        std::reverse(steps.begin(), steps.end());
        // A user call in an index is allowed only for a function that reads nothing but its arguments and constants.
        struct UserCall : ASTVisitor<UserCall, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression& c) {
                if (!c.isSystemCall()) {
                    const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
                    std::unordered_set<const SubroutineSymbol*> seen;
                    found = found || !sub || !*sub || !pureFunction(**sub, seen);
                }
                visitDefault(c);
            }
        } call;
        target.visit(call);
        if (call.found) return false;
        // The variables the indices name must all be proven loop variables.
        std::vector<const LoopValues*> loops;
        bool foreign = false;
        target.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
            if (&sym == root || sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue ||
                sym.kind == SymbolKind::Specparam || sym.kind == SymbolKind::Field)
                return;
            auto it = std::find_if(scope.begin(), scope.end(), [&](const LoopValues& v) { return v.var == &sym; });
            if (it == scope.end()) foreign = true;
            else if (std::find(loops.begin(), loops.end(), &*it) == loops.end()) loops.push_back(&*it);
        });
        if (foreign) return false;
        uint64_t combos = 1;
        for (auto* lv : loops) {
            if (lv->count > kMaxProvenValues || combos * lv->count > kMaxProvenValues) return false;
            combos *= lv->count;
        }

        ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
        EvalContext ec(ac);
        ec.pushEmptyFrame();
        std::vector<ConstantValue*> locals;
        for (auto* lv : loops) locals.push_back(ec.createLocal(lv->var));
        auto evalIndex = [&](const Expression& e) -> std::optional<int64_t> {
            auto cv = e.eval(ec);
            if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
            return cv.integer().as<int64_t>();
        };
        std::vector<Iv> bits;
        for (uint64_t n = 0; n < combos; n++) {
            uint64_t rest = n;
            for (size_t j = 0; j < loops.size(); j++) {
                const auto* lv = loops[j];
                const auto& ty = lv->var->getType();
                *locals[j] = SVInt(static_cast<bitwidth_t>(ty.getBitWidth()), static_cast<uint64_t>(lv->at(rest % lv->count)), ty.isSigned());
                rest /= lv->count;
            }
            // Walk from the root outwards, tracking the flat offset and the type of what is selected so far.
            const Type* t = &root->getType().getCanonicalType();   // null once a single bit is selected
            uint64_t off = 0;
            bool outside = false, partial = false;
            Iv part{0, 0};
            for (size_t k = 0; k < steps.size() && !outside; k++) {
                const Expression& st = *steps[k];
                if (st.kind == ExpressionKind::MemberAccess) {
                    if (!t) return false;
                    const auto* f = st.as<MemberAccessExpression>().member.as_if<FieldSymbol>();
                    const bool agg = t->kind == SymbolKind::PackedStructType || t->kind == SymbolKind::UnpackedStructType ||
                                     t->kind == SymbolKind::PackedUnionType || t->kind == SymbolKind::UnpackedUnionType;
                    if (!f || !agg) return false;
                    off += f->bitOffset;
                    t = &f->getType().getCanonicalType();
                    continue;
                }
                if (!t) return false;
                std::optional<ConstantRange> range;
                uint64_t ew = 1;
                bool packed = true;
                const Type* elem = nullptr;
                if (auto r = packedRangeOf(*t)) range = r;
                else if (t->kind == SymbolKind::FixedSizeUnpackedArrayType) {
                    range = t->getFixedRange();
                    elem = t->getArrayElementType();
                    packed = false;
                }
                else if (t->kind == SymbolKind::PackedArrayType) {
                    range = t->as<PackedArrayType>().range;
                    elem = &t->as<PackedArrayType>().elementType;
                }
                if (!range) return false;
                if (elem) ew = elem->getSelectableWidth();
                if (ew == 0) return false;
                if (st.kind == ExpressionKind::ElementSelect) {
                    auto idx = evalIndex(st.as<ElementSelectExpression>().selector());
                    if (!idx) return false;
                    auto pos = flatPosition(*range, *idx, packed);
                    if (!pos) { outside = true; break; }
                    off += *pos * ew;
                    t = elem ? &elem->getCanonicalType() : nullptr;
                }
                else {   // an indexed or simple part select of a vector: the outermost step
                    const auto& rs = st.as<RangeSelectExpression>();
                    if (elem) return false;
                    auto left = evalIndex(rs.left()), right = evalIndex(rs.right());
                    if (!left || !right) return false;
                    int64_t lo, hi;
                    switch (rs.getSelectionKind()) {
                        case RangeSelectionKind::IndexedUp: lo = *left; hi = *left + *right - 1; break;
                        case RangeSelectionKind::IndexedDown: lo = *left - *right + 1; hi = *left; break;
                        default: lo = std::min(*left, *right); hi = std::max(*left, *right); break;
                    }
                    if (*right <= 0 && rs.getSelectionKind() != RangeSelectionKind::Simple) return false;
                    lo = std::max<int64_t>(lo, range->lower());   // clamp to the vector
                    hi = std::min<int64_t>(hi, range->upper());
                    if (lo > hi) { outside = true; break; }
                    auto p = flatPosition(*range, lo), q = flatPosition(*range, hi);
                    if (!p || !q) { outside = true; break; }
                    part = {std::min(*p, *q), std::max(*p, *q)};
                    partial = true;
                }
            }
            if (outside) continue;
            if (partial) bits.push_back({off + part.first, off + part.second});
            else bits.push_back({off, off + (t ? t->getSelectableWidth() : 1) - 1});
        }
        placed = std::move(bits);
        return true;
    }
    // A loop variable the loop's own body writes (directly or through an output, inout or ref argument) does not step through
    // its range: no value of it, and no bit it selects, is known.
    static bool writesOwnVariable(const ForLoopStatement& s) {
        for (auto* v : s.loopVars)
            if (writesVariable(s.body, v, true)) return true;
        for (auto* e : s.initializers)
            if (e->kind == ExpressionKind::Assignment) {
                const Expression& target = unwrapImplicit(e->as<AssignmentExpression>().left());
                if (target.kind == ExpressionKind::NamedValue && writesVariable(s.body, &target.as<NamedValueExpression>().symbol))
                    return true;
            }
        return false;
    }
    void handle(const ForLoopStatement& s) {
        // slang's drivers are exact only for a loop it unrolled: constant start, bound and step
        const bool zeroTrips = neverRuns(s, l);   // its writes are not hardware (the initialisers still run once)
        for (const auto* init : s.initializers)
            if (init->kind == ExpressionKind::Assignment) initTargets.insert(&init->as<AssignmentExpression>().left());
        dead += zeroTrips;
        struct Undo { int& d; bool on; ~Undo() { d -= on; } } undo{dead, zeroTrips};
        const bool exitOnly = !procContinue && leavesEarly(s.body) && constantLoopValues(s, l).has_value();
        const bool unknown = !constantLoopValues(s, l).has_value();
        exitLoops += exitOnly;
        unknownLoops += unknown;
        forLoop(s);
        unknownLoops -= unknown;
        exitLoops -= exitOnly;
    }
    template<typename T> void runtimeLoop(const T& s) {
        ++unknownLoops;
        ConditionalDepthVisitor<DynamicWriteCollector>::handle(s);
        --unknownLoops;
    }
    // A loop whose entry condition (or repeat count) the ties settle runs its body never, or as it may: a body that never runs writes
    // nothing, and one under a tie the analysis cannot settle is unknown, not live.
    template<typename T> void tiedLoop(const T& s, const Expression& cond, bool count) {
        // a constant entry condition is slang's own to settle: its drivers already leave out a body that never runs
        if (l.evalConst(unwrapImplicit(cond)).isInteger()) { runtimeLoop(s); return; }
        tieMixed = false;
        std::optional<bool> runs;
        if (callsSubroutine(cond)) {
            if (readsTie(cond)) tieMixed = true;
        } else if (count) {
            const auto cv = l.evalConst(cond);
            const auto v = cv.isInteger() && !cv.integer().hasUnknown() ? std::optional<SVInt>(cv.integer()) : evalTied(unwrapImplicit(cond));
            if (v) runs = !bool(*v == 0) && !(v->isSigned() && v->isNegative());
            else if (readsTie(cond)) tieMixed = true;
        } else runs = decided(cond);
        const bool mixed = std::exchange(tieMixed, false) && !runs;
        dead += runs && !*runs;
        mixedGuard += mixed;
        runtimeLoop(s);
        mixedGuard -= mixed;
        dead -= runs && !*runs;
    }
    void handle(const WhileLoopStatement& s) { tiedLoop(s, s.cond, false); }
    void handle(const DoWhileLoopStatement& s) { runtimeLoop(s); }
    void handle(const RepeatLoopStatement& s) { tiedLoop(s, s.count, true); }
    // `@(posedge clk iff en)`: the body runs when some event can fire; an `iff` the ties settle to false never lets it.
    void handle(const TimedStatement& s) {
        std::vector<const TimingControl*> events;
        if (s.timing.kind == TimingControlKind::EventList) for (const auto* e : s.timing.as<EventListControl>().events) events.push_back(e);
        else events.push_back(&s.timing);
        bool allDead = true, anyLive = false, anyMixed = false;
        for (const auto* e : events) {
            if (e->kind != TimingControlKind::SignalEvent || !e->as<SignalEventControl>().iffCondition) { allDead = false; anyLive = true; continue; }
            const Expression& iff = *e->as<SignalEventControl>().iffCondition;
            tieMixed = false;
            std::optional<bool> v;
            if (callsSubroutine(iff)) { if (readsTie(iff)) tieMixed = true; } else v = decided(iff);
            const bool mixed = std::exchange(tieMixed, false);
            if (v) { if (*v) { allDead = false; anyLive = true; } }
            else { allDead = false; anyMixed = anyMixed || mixed; if (!mixed) anyLive = true; }
        }
        const bool dies = allDead, mix = !dies && !anyLive && anyMixed;
        dead += dies;
        mixedGuard += mix;
        visitDefault(s);
        mixedGuard -= mix;
        dead -= dies;
    }
    void handle(const ForeverLoopStatement& s) { runtimeLoop(s); }
    void forLoop(const ForLoopStatement& s) {
        if (writesOwnVariable(s)) {
            guard(s);
            return;
        }
        auto values = constantLoopValues(s, l);
        // a counter the header declares, or a module-level temporary the header initialises: nothing outside this procedure reads it
        const bool declared = values && (std::find(s.loopVars.begin(), s.loopVars.end(), values->var) != s.loopVars.end() || temps.count(values->var) != 0);
        if (!declared || leavesEarly(s.body)) {
            guard(s);
            return;
        }
        proven.push_back(*values);
        visitDefault(s);
        proven.pop_back();
    }
    // A disable or return may skip the loops after it, so nothing in such a procedure is certain to run.
    struct ContinueFinder : ASTVisitor<ContinueFinder, VisitFlags::AllGood> {
        bool found = false;
        void handle(const ContinueStatement&) { found = true; }
    };
    bool virtualInterfaces = false;   // some variable, argument or class property is a virtual interface (or an array of them)
    void handle(const VariableSymbol& v) {
        if (v.getType().isVirtualInterfaceOrArray()) virtualInterfaces = true;
        visitDefault(v);
    }
    void handle(const ProceduralBlockSymbol& p) {
        struct Restore { const Symbol*& c; const Symbol* old; ~Restore() { c = old; } } restore{container, std::exchange(container, &p)};
        ContinueFinder cf;
        p.getBody().visit(cf);
        procContinue = cf.found;
        condSyms.clear();
        condCalls = false;
        collectConditions(p.getBody());
        inProcedure = true;
        if (leavesEarly(p.getBody(), true)) guard(p);
        else visitDefault(p);
        inProcedure = false;
        procContinue = false;
    }
    // The value of a condition that elaboration settles: a constant, an `&&` with a constant-false operand (`we && EN` with EN = 0), an `||` with a
    // constant-true one.
    std::optional<bool> decided(const Expression& e0) {
        const Expression& e = unwrapImplicit(e0);
        auto cv = l.evalConst(e);
        if (cv.isInteger() && !cv.integer().hasUnknown()) return !bool(cv.integer() == 0);
        // the value the ties give an expression of tied inputs (`en == 2'd1`, `mode[1]`, a net derived from them)
        if (const auto tied = evalTied(e)) return !bool(*tied == 0);
        if (e.kind == ExpressionKind::NamedValue)
            if (const auto* port = e.as<NamedValueExpression>().symbol.as_if<ValueSymbol>(); port && isInputPort(*port)) {
                if (const auto t = tiedPort(*port, 0)) return t;
                return std::nullopt;
            }
        if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
            if (const auto inner = decided(e.as<UnaryExpression>().operand())) return !*inner;
            return std::nullopt;
        }
        if (e.kind == ExpressionKind::BinaryOp && !callsSubroutine(e) &&
            (e.as<BinaryExpression>().op == BinaryOperator::LogicalAnd || e.as<BinaryExpression>().op == BinaryOperator::LogicalOr)) {
            const auto& b = e.as<BinaryExpression>();
            const auto x = decided(b.left()), y = decided(b.right());
            const bool absorbing = b.op == BinaryOperator::LogicalOr;   // the value that settles the whole: true for `||`, false for `&&`
            if ((x && *x == absorbing) || (y && *y == absorbing)) return absorbing;
            if (x && y) return !absorbing;
            return std::nullopt;
        }
        // any other form: undecided, and a tie it reads that the ties do not settle makes it unknown, not live
        if (readsTie(e)) tieMixed = true;
        return std::nullopt;
    }
    void handle(const ConditionalStatement& s) {
        if (guarded == 0) markCertain(s);
        // a branch a constant condition excludes is not hardware
        std::optional<bool> constant;
        tieMixed = false;
        if (s.conditions.size() == 1 && !s.conditions[0].pattern && !callsSubroutine(*s.conditions[0].expr))
            constant = decided(*s.conditions[0].expr);
        else
            for (const auto& condition : s.conditions)
                if (readsTie(*condition.expr)) tieMixed = true;
        if (!constant) {
            const bool mixed = std::exchange(tieMixed, false);
            const auto before = out.maybeOff;
            mixedGuard += mixed;
            ConditionalDepthVisitor<DynamicWriteCollector>::handle(s);
            mixedGuard -= mixed;
            if (mixed) clearCertainlyWritten(s, before);
            return;
        }
        bool conditional = false;
        for (const auto& condition : s.conditions) {
            if (conditional) ++guarded;
            condition.expr->visit(*this);
            if (conditional) --guarded;
            conditional = true;
        }
        ++guarded;
        dead += !*constant;
        s.ifTrue.visit(*this);
        dead -= !*constant;
        if (s.ifFalse) {
            dead += *constant;
            s.ifFalse->visit(*this);
            dead -= *constant;
        }
        --guarded;
    }
    void handle(const CaseStatement& s) {
        if (guarded == 0) markCertain(s);
        // arms a constant selector excludes are not hardware
        auto [taken, known] = s.getKnownBranch(ec);
        known = known && !callsSubroutine(s.expr) &&
                std::none_of(s.items.begin(), s.items.end(), [](const auto& item) {
                    return std::any_of(item.expressions.begin(), item.expressions.end(), [](auto* e) { return callsSubroutine(*e); });
                });
        bool mixedCase = false;
        if (!known) {
            // a one-bit input every instance ties to the same constant selects its arm as an `if` on it would
            tieMixed = false;
            if (const auto tied = tiedCaseArm(s)) { taken = tied->first; known = true; }
            mixedCase = std::exchange(tieMixed, false);
        }
        if (!known) {
            const auto before = out.maybeOff;
            mixedGuard += mixedCase;
            ConditionalDepthVisitor<DynamicWriteCollector>::handle(s);
            mixedGuard -= mixedCase;
            if (mixedCase) clearCertainlyWritten(s, before);
            return;
        }
        s.expr.visit(*this);
        ++guarded;
        for (const auto& item : s.items) {
            for (auto* e : item.expressions) e->visit(*this);
            dead += item.stmt.get() != taken;
            item.stmt->visit(*this);
            dead -= item.stmt.get() != taken;
        }
        if (s.defaultCase) {
            dead += s.defaultCase != taken;
            s.defaultCase->visit(*this);
            dead -= s.defaultCase != taken;
        }
        --guarded;
    }
    // The arm of a `case` (also `casez`, `casex`, `case inside`) whose selector and labels the ties decide: {the arm statement or null, true}.
    // The comparison is slang's own (widths, wildcards, ranges). When the ties do not decide it but the selector or a label reads a port some
    // instance ties, the case is unknown (`tieMixed`), never live.
    std::optional<std::pair<const Statement*, bool>> tiedCaseArm(const CaseStatement& s) {
        std::vector<const Expression*> parts{&s.expr};
        for (const auto& item : s.items)
            for (auto* e : item.expressions) parts.push_back(e);
        Binds binds;
        bool tieRead = false, calls = false;
        for (const auto* p : parts) {
            calls = calls || callsSubroutine(*p);
            bindTies(*p, binds, tieRead, 0);
        }
        if (!calls && !binds.empty()) {
            ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
            EvalContext tc(ac);
            tc.pushEmptyFrame();
            for (const auto& b : binds) tc.createLocal(b.first, ConstantValue(b.second));
            const auto [taken, ok] = s.getKnownBranch(tc);
            if (ok) return std::pair<const Statement*, bool>{taken, true};
        }
        if (tieRead) tieMixed = true;
        return std::nullopt;
    }
    // Where the outermost undecided guard writes a variable in every arm (an `if` with an `else`, a `case` with a default), every instance
    // writes it whichever way the guard goes: it is not one a tied-off instance leaves undriven.
    void clearCertainlyWritten(const Statement& s, const std::unordered_set<const ValueSymbol*>& before) {
        if (mixedGuard != 0) return;
        std::vector<const Expression*> w;
        certainWrites(s, w, true);
        for (const auto* e : w)
            ValuePath::visitPaths(
                *e, ec, [&](const ValuePath& p) {
                    if (const auto* root = p.rootSymbol(); root && !before.count(root)) {
                        out.maybeOff.erase(root);
                        callCleared.insert(root);
                    }
                },
                /* skipSelectors */ true);
    }
    void markCertain(const Statement& s) {
        std::vector<const Expression*> w;
        certainWrites(s, w);
        certain.insert(w.begin(), w.end());
    }
    void handle(const ForeachLoopStatement& s) {
        const auto mark = foreachVars.size();
        for (auto& d : s.loopDims)
            if (d.loopVar) foreachVars.push_back(d.loopVar);
        ++unknownLoops;
        guard(s);
        --unknownLoops;
        foreachVars.resize(mark);
    }
    // A subroutine's writes happen only when it is called.
    void handle(const SubroutineSymbol& s) {
        allSubs.insert(&s);
        ContinueFinder cf;
        s.getBody().visit(cf);
        const auto outerRoutine = std::exchange(routine, &s);
        const bool outerContinue = std::exchange(procContinue, cf.found);
        ++subroutines;
        guard(s);
        --subroutines;
        routine = outerRoutine;
        procContinue = outerContinue;
    }
    void handle(const CallExpression& c) {
        if (!c.isSystemCall()) {
            out.callSites.push_back(c.sourceRange.start());
            for (const auto* argument : c.arguments()) out.argSites.push_back(argument->sourceRange.start());
            if (const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine); sub && *sub) {
                called.insert(*sub);
                calls.push_back({*sub, routine, dead != 0 ? kDead : mixedGuard != 0 ? kMixed : kLive, c.sourceRange.start(), routine ? nullptr : container});
            }
        }
        visitDefault(c);
    }
    // The variables a subroutine's own statements write (not those of the subroutines it calls), with where each write sits.
    struct BodyWrites {
        std::vector<std::pair<const ValueSymbol*, slang::SourceLocation>> writes;
    };
    BodyWrites bodyWrites(const SubroutineSymbol& sub) {
        BodyWrites bw;
        struct Writes : ASTVisitor<Writes, VisitFlags::AllGood> {
            DynamicWriteCollector& c;
            BodyWrites& bw;
            Writes(DynamicWriteCollector& x, BodyWrites& b) : c(x), bw(b) {}
            void note(const Expression& lhs) {
                ValuePath::visitPaths(
                    lhs, c.ec, [&](const ValuePath& p) {
                        if (const auto* root = p.rootSymbol()) bw.writes.emplace_back(root, p.rootExpr->sourceRange.start());
                    },
                    /* skipSelectors */ true);
            }
            void handle(const AssignmentExpression& a) { note(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
                    u.op == UnaryOperator::Postdecrement)
                    note(u.operand());
                visitDefault(u);
            }
        } w(*this, bw);
        sub.getBody().visit(w);
        return bw;
    }
    // Every call to a user subroutine, with the state of the code around it: a call reaches its callee only as live as the code that
    // holds it, and a subroutine's writes count only through the calls that reach it (settleSubroutines).
    static constexpr int kDead = 0, kMixed = 1, kLive = 2;   // ordered: the weakest of a call and its caller's reach wins
    struct CallRec { const SubroutineSymbol* callee; const SubroutineSymbol* caller; int state; slang::SourceLocation at; const Symbol* container; };
    std::vector<CallRec> calls;
    std::unordered_set<const SubroutineSymbol*> allSubs;
    std::unordered_set<const ValueSymbol*> callCleared;   // written through a call in every arm of a guard some instances tie off
    // How far each subroutine is reached: kLive when some live call, from live code, reaches it; kMixed when only calls under a
    // guard some instances tie off do; kDead when no call does, or every one sits in dead code, a call cycle reached only from dead calls included.
    void settleSubroutines() {
        std::unordered_map<const SubroutineSymbol*, int> reach;
        // Roots whose callers this scan does not see are live, and so are the callees of their bodies: a class method (an override is
        // reached through its base), a subroutine declared in a scope other than a module, generate block, package or compilation unit,
        // a DPI export, and, once any virtual interface exists, every subroutine of an interface (a call through one names the
        // interface's declaration, not an instance's).
        for (const auto& e : l.compilation().getDPIExports())
            if (e.subroutine) reach[e.subroutine] = kLive;
        for (const auto* sub : allSubs) {
            const auto kind = sub->getParentScope() ? sub->getParentScope()->asSymbol().kind : SymbolKind::Unknown;
            if (kind != SymbolKind::InstanceBody && kind != SymbolKind::GenerateBlock && kind != SymbolKind::Package &&
                kind != SymbolKind::CompilationUnit)
                reach[sub] = kLive;
            else if (virtualInterfaces)
                for (const auto* sc = sub->getParentScope(); sc; sc = sc->asSymbol().getParentScope())
                    if (const auto* body = sc->asSymbol().as_if<InstanceBodySymbol>(); body && body->getDefinition().definitionKind == DefinitionKind::Interface) {
                        reach[sub] = kLive;
                        break;
                    }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& r : calls) {
                int state = r.state;
                if (r.caller) state = std::min(state, reach[r.caller]);
                if (state > reach[r.callee]) { reach[r.callee] = state; changed = true; }
            }
        }
        for (const auto& w : pendingLive)
            if (reach[w.routine] != kDead) {   // a mixed one counts: its write is Unresolved, not dead
                out.liveWrites.insert(w.root);
                if (w.bits) out.liveBits[w.root].push_back(*w.bits);
            }
        for (const auto* sub : allSubs) {
            const int state = reach[sub];
            if (state == kLive) continue;
            const auto bw = bodyWrites(*sub);
            if (state == kMixed) {
                for (const auto& w : bw.writes)
                    if (!callCleared.count(w.first)) out.maybeOff.insert(w.first);
                continue;
            }
            for (const auto& [root, at] : bw.writes) {
                out.deadWrites.insert(root);
                out.deadSites.emplace_back(at, nullptr);   // the driver inside the body belongs to the subroutine
            }
            for (const auto& r : calls)
                if (r.caller == sub) out.deadSites.emplace_back(r.at, nullptr);   // slang lists a driver at each call inside the body too
        }
        for (const auto& r : calls)
            if (std::min(r.state, r.caller ? reach[r.caller] : kLive) == kDead && reach[r.callee] == kDead) out.deadSites.emplace_back(r.at, r.container);
    }
    void handle(const AssignmentExpression& a) {
        noteLhs(a.left());
        visitDefault(a);
    }
    // Continuous assignments are visited explicitly so their targets are never skipped.
    void handle(const ContinuousAssignSymbol& ca) {
        auto& e = ca.getAssignment();
        struct Restore { const Symbol*& c; const Symbol* old; ~Restore() { c = old; } } restore{container, std::exchange(container, &ca)};
        if (e.kind == ExpressionKind::Assignment) noteLhs(e.as<AssignmentExpression>().left());
        visitDefault(ca);
    }
};

DynWrites dynamicWritesOf(Lint& l) {
    ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
    EvalContext ec(ac);
    DynamicWriteCollector c(l, ec, model::getModel(l.context().design).procedureTemps);
    l.visitHardware(c);
    for (const auto& w : c.pending)
        if (!w.routine || c.called.count(w.routine)) c.out.staticWrites[w.root].push_back(w.bounds);
    c.settleSubroutines();
    return std::move(c.out);
}

// A static write in the code that none of the symbol's drivers covers: slang dropped it (after a `continue` that runs, in a loop it
// takes for never executing), so it is a possible driver of bits the driver list leaves undriven. The symbol then counts like one
// with a write through an index no proof places: its gaps are Unresolved, and "never driven" is not claimed.
void markFloor(DynWrites& dw, const ValueSymbol& sym, const Drivers& drivers) {
    auto it = dw.staticWrites.find(&sym);
    if (it == dw.staticWrites.end()) return;
    std::vector<Iv> listed;
    for (auto* d : drivers) listed.push_back(d->getBounds());
    std::sort(listed.begin(), listed.end());
    auto covered = [&](Iv b) {
        uint64_t at = b.first;
        for (auto& iv : listed) {
            if (iv.second < at) continue;
            if (iv.first > at) return false;
            if (iv.second >= b.second) return true;
            at = iv.second + 1;
        }
        return false;
    };
    if (std::all_of(it->second.begin(), it->second.end(), covered)) return;
    if (!dw.moduleIdx.count(&sym) && !dw.loopIdx.count(&sym)) dw.floorOnly.insert(&sym);
    dw.loopIdx.insert(&sym);
    dw.unprovenLoop.insert(&sym);
}

// Coverage from the drivers slang reports, without those of writes whose index is not constant after unrolling: slang gives
// such a write a driver of the whole range it could not place (a driver whose expression is the symbol's own name), which
// would hide the gaps.
Drivers staticDrivers(const Drivers& ds, const DynWrites& dw, const ValueSymbol* sym = nullptr) {
    Drivers kept;
    const bool hasDead = sym && dw.deadWrites.count(sym);
    const auto has = [](const std::vector<slang::SourceLocation>& v, slang::SourceLocation at) { return std::find(v.begin(), v.end(), at) != v.end(); };
    for (auto* d : ds) {
        const auto at = d->getSourceRange().start();
        const bool fromCall = has(dw.callSites, at) && dw.subroutineDynamicWrites.count(&d->getSymbol()) != 0;
        if (hasDead && atDeadSite(dw, *d)) continue;   // a dead write is no hardware: the bits its driver names are live only where a live write covers them
        if (!has(dw.dynamicWriteRoots, at) && !fromCall) kept.push_back(d);
    }
    return kept;
}
Coverage staticCoverage(const Drivers& ds, const DynWrites& dw, const ValueSymbol* sym = nullptr) {
    auto c = coverageOf(staticDrivers(ds, dw, sym));
    if (sym && dw.deadWrites.count(sym))
        if (auto it = dw.liveBits.find(sym); it != dw.liveBits.end()) c.iv.insert(c.iv.end(), it->second.begin(), it->second.end());
    return c;
}

// The bits no static driver drives, plus those a proven loop write drives.
std::vector<Iv> undrivenBits(const Type& t, const ValueSymbol& sym, const Coverage& cov, const DynWrites& dw, bool withRange = false) {
    auto ivs = cov.iv;
    {
        if (auto p = dw.provenBits.find(&sym); p != dw.provenBits.end()) ivs.insert(ivs.end(), p->second.begin(), p->second.end());
        if (withRange)
            if (auto p = dw.rangeBits.find(&sym); p != dw.rangeBits.end()) ivs.insert(ivs.end(), p->second.begin(), p->second.end());
    }
    return complementOf(std::move(ivs), t.getSelectableWidth());
}

// The bits the writes themselves leave undriven: the gaps without the drivers listed at the arguments of calls.
std::vector<Iv> writtenGaps(const Type& t, const ValueSymbol& sym, const Drivers& drivers, const DynWrites& dw, bool withRange = false) {
    Drivers written;
    for (auto* d : drivers)
        if (std::find(dw.argSites.begin(), dw.argSites.end(), d->getSourceRange().start()) == dw.argSites.end()) written.push_back(d);
    return undrivenBits(t, sym, staticCoverage(written, dw, &sym), dw, withRange);
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-008: a plain vector or scalar with at least one driver whose drivers leave some bits undriven.
// Fully undriven symbols are slang's undriven diagnostics, except one whose only writes are dead code (slang sees the writes).
class UndrivenBitsRule final : public LintRule {
public:
    // upstream: the fully-undriven half of the title is slang's own diagnostic (mapped to LNT-008).
    UndrivenBitsRule()
        : LintRule("LNT-008", "", true) {}

private:
    // slang calls a signal that is only wired to an instance of an unknown module "never assigned". The unknown module's outputs may
    // drive it, so nothing is established: the finding stays, Unresolved, and names the module (LNT-002 reports the module itself).
    static void unknownInstanceConnections(Lint& l) {
        struct Wired : HardwareVisitor<Wired, VisitFlags::AllCanonical> {
            std::map<std::pair<std::string, uint32_t>, std::string> byLine;
            Lint& l;
            explicit Wired(Lint& lint) : l(lint) {}
            struct Names : ASTVisitor<Names, VisitFlags::AllGood> {
                std::vector<const Symbol*> found;
                void handle(const NamedValueExpression& e) { found.push_back(&e.symbol); }
            };
            void handle(const UninstantiatedDefSymbol& u) {
                Names names;
                for (const auto* connection : u.getPortConnections())
                    if (connection) connection->visit(names);
                for (const auto* sym : names.found) {
                    const auto at = l.loc(sym->location);
                    byLine.emplace(std::pair{at.file, at.line}, std::string(u.definitionName));
                }
                visitDefault(u);
            }
        } wired(l);
        l.visitHardware(wired);
        if (wired.byLine.empty()) return;
        for (auto& d : l.context().report.mutableDiagnostics()) {
            if (d.ruleId != "LNT-008" || d.origin != "slang" || d.confidence == Confidence::Unresolved) continue;
            const auto it = wired.byLine.find({d.location.file, d.location.line});
            if (it == wired.byLine.end()) continue;
            d.confidence = Confidence::Unresolved;
            d.message += " (it is wired to an instance of the unknown module '" + it->second + "', which may drive it)";
            d.nextStep = "Provide the module (or a --library entry for it) so its outputs are known.";
            d.applyDefaultDisposition();
        }
    }
    // slang calls an `inout` port that the module only reads "undriven output port". It is used as an input: the net it is wired to
    // supplies the value, and nothing in the module could drive it. The hardware is not wrong, so the finding is INFO and says so.
    static void readOnlyInouts(Lint& l) {
        struct Inouts : HardwareVisitor<Inouts, VisitFlags::AllCanonical> {
            std::set<std::tuple<std::string, uint32_t, std::string>> at;
            Lint& l;
            explicit Inouts(Lint& lint) : l(lint) {}
            void handle(const PortSymbol& p) {
                if (p.direction == ArgumentDirection::InOut && l.isUser(p.location)) {
                    const auto loc = l.loc(p.location);
                    at.insert({loc.file, loc.line, std::string(p.name)});
                }
                visitDefault(p);
            }
        } inouts(l);
        l.visitHardware(inouts);
        if (inouts.at.empty()) return;
        for (auto& d : l.context().report.mutableDiagnostics()) {
            if (d.ruleId != "LNT-008" || d.origin != "slang" || d.upstreamCode != "undriven-port" || d.severity != Severity::Warning) continue;
            for (const auto& [file, line, name] : inouts.at)
                if (d.location.file == file && d.location.line == line && d.message.find("'" + name + "'") != std::string::npos) {
                    d.severity = Severity::Info;
                    d.message = "inout port '" + name + "' is only read in this module: it never drives its net and acts as an input";
                    d.nextStep = "Declare the port input if nothing in the module is meant to drive it.";
                    d.applyDefaultDisposition();
                    break;
                }
        }
    }
    void check(Lint& l) override {
        unknownInstanceConnections(l);
        readOnlyInouts(l);
        auto* am = analysisOf(l);
        if (!am) return;
        ValueCollector c;
        l.visitHardware(c);
        auto dw = dynamicWritesOf(l);
        const auto pragmas = readUserPragmas(l.compilation());
        for (auto& e : c.vals) {
            const Type& t = e.sym->getType();
            if (e.port || !l.isUser(e.sym->location)) continue;
            auto drivers = am->getDrivers(*e.sym);
            bool offDrivers = false;
            drivers = synthesisDrivers(l, pragmas, drivers, offDrivers);
            if (offDrivers && drivers.empty() && !dw.moduleIdx.count(e.sym) && !dw.loopIdx.count(e.sym)) {
                l.count();
                auto name = std::string(e.sym->name);
                l.report(*e.sym, "'" + name + "' is never driven for synthesis: every driver lies in a translate_off region",
                         {.nextStep = "Drive the signal in code that is synthesized, or remove the translate_off around its driver.", .signals = {name}});
                continue;
            }
            markFloor(dw, *e.sym, drivers);
            if (onlyDeadWrites(dw, *e.sym, drivers)) {
                l.count();
                auto name = std::string(e.sym->name);
                l.report(*e.sym, "variable '" + name + "' is never driven: every write to it is dead code",
                         {.nextStep = "Drive the variable in code that runs, or remove it.", .signals = {name}});
                continue;
            }
            if (!t.isIntegral() || isComposite(t)) continue;
            bool modW = dw.moduleIdx.count(e.sym) != 0, locW = dw.loopIdx.count(e.sym) != 0;
            if (drivers.empty() && !modW && !locW) continue;
            l.count();
            auto name = std::string(e.sym->name);
            auto cov = staticCoverage(drivers, dw, e.sym);
            auto gaps = undrivenBits(t, *e.sym, cov, dw);
            // A write through a non-constant index that no loop proof accounts for may drive any gap.
            if (cov.dynamic || ((modW || dw.unprovenLoop.count(e.sym) != 0) && !gaps.empty())) {
                reportUnresolved(l, e.sym->location, unknownCoverage(t, name, "", gaps, dw.floorOnly.count(e.sym) != 0), e.sym->getHierarchicalPath());
                continue;
            }
            if (gaps.empty()) {
                if (dw.maybeOff.count(e.sym) && !dw.alwaysWritten.count(e.sym))
                    reportUnresolved(l, e.sym->location,
                                     "'" + name + "' is written only under a condition on an input that some instance ties to a constant, and the tie does not decide it for every instance (the instances differ, or the tie covers only part of the condition): an instance that ties it off never drives its copy",
                                     e.sym->getHierarchicalPath());
                continue;
            }
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
        l.visitHardware(c);
        auto dw = dynamicWritesOf(l);
        const auto pragmas = readUserPragmas(l.compilation());
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
            bool offDrivers = false;
            drivers = synthesisDrivers(l, pragmas, drivers, offDrivers);
            if (offDrivers && drivers.empty() && !dw.moduleIdx.count(e.sym) && !dw.loopIdx.count(e.sym)) {
                l.count();
                l.report(*e.port, "output '" + name + "' is never driven for synthesis: every driver lies in a translate_off region",
                         {.nextStep = "Drive the output in code that is synthesized, or remove the translate_off around its driver.", .signals = {name}});
                continue;
            }
            markFloor(dw, *e.sym, drivers);
            bool modW = dw.moduleIdx.count(e.sym) != 0, locW = dw.loopIdx.count(e.sym) != 0;
            l.count();
            if (onlyDeadWrites(dw, *e.sym, drivers)) {
                l.report(*e.port, "output '" + name + "' is never driven: every write to it is dead code",
                         {.nextStep = "Drive the output in code that runs, or remove the port.", .signals = {name}});
                continue;
            }
            if (drivers.empty() && !modW && !locW) {
                l.report(*e.port, "output '" + name + "' is never driven",
                         {.nextStep = "Drive the output in this module or remove the port.", .signals = {name}});
                continue;
            }
            auto cov = staticCoverage(drivers, dw, e.sym);
            auto gaps = undrivenBits(t, *e.sym, cov, dw);
            // Composite outputs: a write through a non-constant index that no loop proof accounts for is Unresolved.
            const bool unresolvedWrite = (modW || dw.unprovenLoop.count(e.sym) != 0) && !writtenGaps(t, *e.sym, drivers, dw).empty();
            if (cov.dynamic || unresolvedWrite) {
                reportUnresolved(l, e.port->location, unknownCoverage(t, name, "output", gaps, dw.floorOnly.count(e.sym) != 0), e.port->getHierarchicalPath());
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
        l.visitHardware(c);
        auto dw = dynamicWritesOf(l);
        for (auto& e : c.vals) {
            if (e.port || !l.isUser(e.sym->location)) continue;
            const Type& t = e.sym->getType();
            bool iface = e.body && e.body->getDefinition().definitionKind == DefinitionKind::Interface;
            if (!isComposite(t) && !iface && !isDynamicContainer(t)) continue;
            auto name = std::string(e.sym->name);
            auto drivers = am->getDrivers(*e.sym);
            markFloor(dw, *e.sym, drivers);
            if (onlyDeadWrites(dw, *e.sym, drivers)) continue;   // never written at all: LNT-008 says so
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
            // A write through a design value places the bits that value's range names (`placeByRange`): the members it cannot reach stay
            // undriven for certain, the others count as driven. One the range does not decide keeps the symbol Unresolved.
            const bool modU = modW && dw.moduleUndecided.count(e.sym) != 0;
            auto cov = staticCoverage(drivers, dw, e.sym);
            auto gaps = undrivenBits(t, *e.sym, cov, dw, true);
            // A write through a run-time index places bits only where the static drivers leave a gap: after a whole write there is none.
            // A driver slang lists at an actual argument of a call (a `ref` argument the callee may only read) is no write: it closes no gap.
            // (A gap that only a driver listed at an argument closes is no proof either, whatever the indices are.)
            if (cov.dynamic || ((modU || dw.unprovenLoop.count(e.sym) != 0 || (modW && gaps.empty())) && !writtenGaps(t, *e.sym, drivers, dw, true).empty())) {
                reportUnresolved(l, e.sym->location, unknownCoverage(t, name, "", gaps, dw.floorOnly.count(e.sym) != 0), e.sym->getHierarchicalPath());
                continue;
            }
            if (gaps.empty()) {
                if (dw.maybeOff.count(e.sym) && !dw.alwaysWritten.count(e.sym))
                    reportUnresolved(l, e.sym->location,
                                     "'" + name + "' is written only under a condition on an input that some instance ties to a constant, and the tie does not decide it for every instance (the instances differ, or the tie covers only part of the condition): an instance that ties it off never drives its copy",
                                     e.sym->getHierarchicalPath());
                // What only an unproven index range reaches is possibly written, not shown to be: no finding without that proof.
                if (dw.rangeUnproven.count(e.sym)) {
                    auto ivs = cov.iv;
                    if (auto p = dw.provenBits.find(e.sym); p != dw.provenBits.end()) ivs.insert(ivs.end(), p->second.begin(), p->second.end());
                    const auto reach = complementOf(std::move(ivs), t.getSelectableWidth());
                    if (!reach.empty())
                        reportUnresolved(l, e.sym->location, unknownCoverage(t, name, "", reach, false), e.sym->getHierarchicalPath());
                }
                continue;
            }
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
    l.visitHardware(c);
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
        l.visitHardware(c);
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
                         {.severity = Severity::Warning, .confidence = Confidence::Established,
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
using Interval = std::pair<uint64_t, uint64_t>;   // inclusive, selector bit patterns (see below)

struct Arm {
    const Expression* expr = nullptr;
    bool range = false, never = false;
    uint64_t val = 0, care = 0;   // pattern, in the bits of the selector
    std::vector<Interval> iv;     // inside: the selector patterns matched
    size_t group = 0;             // the case item the arm belongs to
};

// `case inside` types each comparison on its own (slang evaluates a range `[lo:hi]` as one comparison pair): it is signed only
// when the selector and both bounds of that item are signed, and unsigned otherwise, whatever the other items are. An arm
// is the set of selector bit patterns it matches, as unsigned intervals; a range whose left bound exceeds its right one
// (in that signedness) matches nothing.
// Width and signedness of an `inside` item as it is typed on its own. slang types all items of a case together (one common type,
// pushed into the operators of every item), which the LRM does not: each comparison takes the type of its own operands. The
// operators that pass their type down are followed to their operands; one whose value depends on the width it is evaluated in
// (a division, a right shift, a mix of narrow signed and unsigned operands) is not modelled.
struct SelfType {
    uint32_t width;
    bool isSigned;
};

std::optional<SelfType> selfType(const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    auto join = [](const std::optional<SelfType>& a, const std::optional<SelfType>& b) -> std::optional<SelfType> {
        if (!a || !b) return std::nullopt;
        if (a->isSigned != b->isSigned && a->width != b->width) return std::nullopt;
        return SelfType{std::max(a->width, b->width), a->isSigned && b->isSigned};
    };
    if (e.kind == ExpressionKind::UnaryOp) {
        auto& u = e.as<UnaryExpression>();
        if (u.op == UnaryOperator::Plus || u.op == UnaryOperator::Minus || u.op == UnaryOperator::BitwiseNot) return selfType(u.operand());
    }
    else if (e.kind == ExpressionKind::BinaryOp) {
        auto& b = e.as<BinaryExpression>();
        switch (b.op) {
            case BinaryOperator::Add:
            case BinaryOperator::Subtract:
            case BinaryOperator::Multiply:
            case BinaryOperator::BinaryAnd:
            case BinaryOperator::BinaryOr:
            case BinaryOperator::BinaryXor:
            case BinaryOperator::BinaryXnor: return join(selfType(b.left()), selfType(b.right()));
            case BinaryOperator::LogicalShiftLeft:
            case BinaryOperator::ArithmeticShiftLeft: return selfType(b.left());
            case BinaryOperator::Divide:
            case BinaryOperator::Mod:
            case BinaryOperator::Power:
            case BinaryOperator::LogicalShiftRight:
            case BinaryOperator::ArithmeticShiftRight: return std::nullopt;
            default: break;   // comparisons and logical operators are self-determined: the common type never reaches them
        }
    }
    else if (e.kind == ExpressionKind::ConditionalOp) {
        auto& c = e.as<ConditionalExpression>();
        return join(selfType(c.left()), selfType(c.right()));
    }
    if (!e.type->isIntegral()) return std::nullopt;
    return SelfType{static_cast<uint32_t>(e.type->getBitWidth()), e.type->isSigned()};
}

// Builds the arm for an item, matched against a selector of `selW` bits (case: the item is extended to the widest of
// selector and item, IEEE 1800 12.5, so item bits above the selector must be zero, or equal to its sign bit when both
// are signed; inside: see Interval). `sgn` is whether the selector is signed. False when the item is not a constant this
// analysis models.
bool buildArm(Lint& l, const Expression& e, CaseStatementCondition cond, uint32_t selW, bool sgn, Arm& a,
              std::optional<bool> commonSigned = std::nullopt) {
    a.expr = &e;
    if (cond == CaseStatementCondition::Inside) {
        const Expression* lo = &e;
        const Expression* hi = &e;
        if (e.kind == ExpressionKind::ValueRange) {
            lo = &e.as<ValueRangeExpression>().left();
            hi = &e.as<ValueRangeExpression>().right();
        }
        auto cl = l.evalConst(*lo), ch = l.evalConst(*hi);
        if (!cl.isInteger() || !ch.isInteger() || selW > 64) return false;
        const SVInt &il = cl.integer(), &ih = ch.integer();
        auto tl = selfType(*lo), th = selfType(*hi);
        if (!tl || !th || il.hasUnknown() || ih.hasUnknown() || tl->width > 64 || th->width > 64 || il.getBitWidth() < tl->width ||
            ih.getBitWidth() < th->width)
            return false;
        const bool isSigned = commonSigned ? *commonSigned : sgn && tl->isSigned && th->isSigned;
        auto value = [&](const SVInt& x, uint32_t bw) {   // the bound, at its own width, as an integer of the comparison's signedness
            unsigned __int128 raw = x.getRawPtr()[0];
            if (bw < 64) raw &= (static_cast<unsigned __int128>(1) << bw) - 1;
            if (isSigned && bw > 0 && ((raw >> (bw - 1)) & 1)) return static_cast<__int128>(raw) - (static_cast<__int128>(1) << bw);
            return static_cast<__int128>(raw);
        };
        const __int128 full = static_cast<__int128>(1) << selW;
        const __int128 dmin = isSigned ? -(full >> 1) : 0, dmax = isSigned ? (full >> 1) - 1 : full - 1;
        const __int128 from = std::max(value(il, tl->width), dmin), to = std::min(value(ih, th->width), dmax);
        a.range = true;
        if (from <= to) {
            auto pat = [&](__int128 x) { return static_cast<uint64_t>(x < 0 ? x + full : x); };
            if (from < 0 && to >= 0) {
                a.iv.push_back({pat(from), pat(full - 1)});
                a.iv.push_back({0, pat(to)});
            }
            else {
                a.iv.push_back({pat(from), pat(to)});
            }
        }
        a.never = a.iv.empty();
        return true;
    }
    auto cv = l.evalConst(e);
    if (!cv.isInteger()) return false;
    const SVInt& v = cv.integer();
    const uint32_t width = std::max<uint32_t>(selW, v.getBitWidth());
    if (width > 64) return false;
    const bool signExt = v.isSigned() && sgn;
    // Bits of the item and of the selector (extended to `width`), one position at a time.
    bool upperOne = false, upperZero = false;
    for (uint32_t i = 0; i < width; i++) {
        logic_t bit = i < v.getBitWidth() ? v[i] : signExt ? v[v.getBitWidth() - 1] : logic_t(0);
        bool isX = bit.value == logic_t::x.value, isZ = bit.value == logic_t::z.value;
        bool wildcard = (isZ && cond == CaseStatementCondition::WildcardJustZ) ||
                        ((isX || isZ) && cond == CaseStatementCondition::WildcardXOrZ);
        bool known = !isX && !isZ;
        if (i >= selW) {
            // The selector has no such bit: it reads as zero (or repeats its sign bit).
            if (known) (bit.value == 1 ? upperOne : upperZero) = true;
            else if (!wildcard) a.never = true;
            continue;
        }
        uint64_t m = uint64_t(1) << i;
        if (known) {
            a.care |= m;
            if (bit.value == 1) a.val |= m;
        }
        else if (!wildcard) {
            a.never = true;   // an unknown bit that is not a wildcard never matches a two-state value
        }
    }
    if (signExt) {
        // Upper bits equal the selector's sign bit: they must agree with each other and with the item's own top bit.
        if (upperOne && upperZero) a.never = true;
        else if (upperOne || upperZero) {
            uint64_t m = uint64_t(1) << (selW - 1);
            if ((a.care & m) && (((a.val & m) != 0) != upperOne)) a.never = true;
            a.care |= m;
            if (upperOne) a.val |= m;
            else a.val &= ~m;
        }
    }
    else if (upperOne) {
        a.never = true;   // a one above the selector's width can never match a zero-extended selector
    }
    return true;
}

// slang types the selector and every item of a `case inside` as one expression: the set is signed only when all of it is. The LRM
// types each comparison on its own, as buildArm does without `commonSigned`. This is the signedness of slang's reading, or nullopt
// when an item's type is not modelled.
std::optional<bool> commonInsideSigned(const CaseStatement& c, bool selectorSigned) {
    bool isSigned = selectorSigned;
    for (auto& g : c.items)
        for (auto* e : g.expressions) {
            const Expression* lo = e;
            const Expression* hi = e;
            if (e->kind == ExpressionKind::ValueRange) {
                lo = &e->as<ValueRangeExpression>().left();
                hi = &e->as<ValueRangeExpression>().right();
            }
            auto tl = selfType(*lo), th = selfType(*hi);
            if (!tl || !th) return std::nullopt;
            isSigned = isSigned && tl->isSigned && th->isSigned;
        }
    return isSigned;
}

bool overlaps(const Arm& a, const Arm& b) {
    if (a.never || b.never) return false;
    if (a.range && b.range) {
        for (auto& x : a.iv)
            for (auto& y : b.iv)
                if (x.first <= y.second && y.first <= x.second) return true;
        return false;
    }
    if (a.range != b.range) return false;
    return ((a.val ^ b.val) & a.care & b.care) == 0;
}

// `v` is a selector value of `selW` bits.
bool matches(const Arm& a, uint64_t v) {
    if (a.never) return false;
    if (a.range) {
        for (auto& x : a.iv)
            if (x.first <= v && v <= x.second) return true;
        return false;
    }
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
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const CaseStatement& c) {
            if (c.condition == CaseStatementCondition::Inside && l.isUser(c.sourceRange.start())) check(c);
            visitDefault(c);
        }
        void check(const CaseStatement& c) {
            l.count();
            uint32_t w = c.expr.unwrapImplicitConversions().type->getBitWidth();
            const bool sgn = c.expr.unwrapImplicitConversions().type->isSigned();
            // The set is read both ways, as for LNT-033: per comparison (the LRM) and with slang's one common type.
            const std::optional<bool> commonSigned = commonInsideSigned(c, sgn);
            std::vector<Arm> arms, commonArms;
            bool unresolved = !commonSigned;
            for (auto& g : c.items)
                for (auto* e : g.expressions) {
                    Arm a, b;
                    if (!unresolved && buildArm(l, *e, c.condition, w, sgn, a) && buildArm(l, *e, c.condition, w, sgn, b, commonSigned)) {
                        arms.push_back(a);
                        commonArms.push_back(b);
                    }
                    else unresolved = true;
                }
            if (unresolved) {
                l.report(c.sourceRange.start(), "'case inside' overlap not determinable: an item is not a constant",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                          .nextStep = "Review the items for overlap manually."});
                return;
            }
            // For each later item: an earlier item that overlaps under both typings establishes it; otherwise one that differs
            // between them leaves it Unresolved. Which comes first in the item list does not matter.
            for (size_t j = 1; j < arms.size(); j++) {
                bool both = false, differs = false;
                for (size_t i = 0; i < j && !both; i++) {
                    const bool first = overlaps(arms[i], arms[j]), second = overlaps(commonArms[i], commonArms[j]);
                    both = first && second;
                    differs = differs || first != second;
                }
                if (both)
                    l.report(arms[j].expr->sourceRange.start(), "'inside' item overlaps a previous item: a value matches both (first match wins)",
                             {.nextStep = "Make the items disjoint, or drop the redundant item."});
                else if (differs)
                    l.report(arms[j].expr->sourceRange.start(),
                             "'case inside' overlap not determinable: whether this item overlaps an earlier one depends on how the set is typed",
                             {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                              .nextStep = "Give the items and the selector the same signedness, so that every reading agrees."});
            }
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// Two side-effect-free conditions that are the same expression: same symbols, operators, selects and constants.
// Anything this does not model (calls, assignments, increments, patterns) is not the same.
bool sameCondition(const Expression& a, const Expression& b) {
    if (a.kind != b.kind || !a.type->isMatching(*b.type)) return false;
    switch (a.kind) {
        case ExpressionKind::NamedValue:
            return &a.as<NamedValueExpression>().symbol == &b.as<NamedValueExpression>().symbol;
        case ExpressionKind::IntegerLiteral:
        case ExpressionKind::UnbasedUnsizedIntegerLiteral:
            {
            auto *x = a.getConstant(), *y = b.getConstant();
            return x && y && x->isInteger() && y->isInteger() && exactlyEqual(x->integer(), y->integer());
        }
        case ExpressionKind::Conversion:
            return a.as<ConversionExpression>().conversionKind == b.as<ConversionExpression>().conversionKind &&
                   sameCondition(a.as<ConversionExpression>().operand(), b.as<ConversionExpression>().operand());
        case ExpressionKind::UnaryOp: {
            auto &x = a.as<UnaryExpression>(), &y = b.as<UnaryExpression>();
            if (x.op != y.op) return false;
            switch (x.op) {
                case UnaryOperator::Preincrement: case UnaryOperator::Predecrement:
                case UnaryOperator::Postincrement: case UnaryOperator::Postdecrement: return false;
                default: return sameCondition(x.operand(), y.operand());
            }
        }
        case ExpressionKind::BinaryOp: {
            auto &x = a.as<BinaryExpression>(), &y = b.as<BinaryExpression>();
            return x.op == y.op && sameCondition(x.left(), y.left()) && sameCondition(x.right(), y.right());
        }
        case ExpressionKind::ElementSelect: {
            auto &x = a.as<ElementSelectExpression>(), &y = b.as<ElementSelectExpression>();
            return sameCondition(x.value(), y.value()) && sameCondition(x.selector(), y.selector());
        }
        case ExpressionKind::RangeSelect: {
            auto &x = a.as<RangeSelectExpression>(), &y = b.as<RangeSelectExpression>();
            return x.getSelectionKind() == y.getSelectionKind() && sameCondition(x.value(), y.value()) &&
                   sameCondition(x.left(), y.left()) && sameCondition(x.right(), y.right());
        }
        default: return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-033: `unique`, `unique0` and `priority` claims on case and if statements.
//  - unique/unique0 case: two items match the same value -> ERROR (the claim is statically false)
//  - unique/priority case without default: uncovered values (or X/Z of a four-state selector) -> WARNING
//  - unique/priority if without final else -> WARNING; unique if with two true constant conditions, or two conditions
//    that are the same expression -> ERROR
//  - non-constant items or conditions, or wide selectors -> WARNING Unresolved, once per statement (LNT-033 for
//    unique/unique0, LNT-081 for priority and for coverage)
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
        const bool sgn = sel.type->isSigned();
        std::vector<Arm> arms, commonArms;   // commonArms: an `inside` set read the way slang types it
        bool unresolved = w > 64;
        const bool inside = c.condition == CaseStatementCondition::Inside;
        const std::optional<bool> commonSigned = inside ? commonInsideSigned(c, sgn) : std::nullopt;
        if (inside && !commonSigned) unresolved = true;
        size_t group = 0;
        for (auto& g : c.items) {
            for (auto* e : g.expressions) {
                Arm a, b;
                auto condition = c.condition;
                if (!overlapRule && inside && e->kind != ExpressionKind::ValueRange) {
                    auto value = l.evalConst(*e);
                    if (value.isInteger() && value.integer().hasUnknown()) condition = CaseStatementCondition::WildcardXOrZ;
                }
                if (!unresolved && buildArm(l, *e, condition, w, sgn, a) && (!inside || buildArm(l, *e, condition, w, sgn, b, commonSigned))) {
                    a.group = b.group = group;
                    arms.push_back(a);
                    if (inside) commonArms.push_back(b);
                }
                else unresolved = true;
            }
            group++;
        }
        if (unresolved) {
            // The overlap claim of unique/unique0 is LNT-033's; LNT-081 owns the coverage of a priority case.
            const bool checkApplies = overlapRule ? chk != UniquePriorityCheck::Priority
                                                  : chk == UniquePriorityCheck::Priority && !c.defaultCase;
            if (checkApplies)
                l.report(c.sourceRange.start(), "'" + kw + "' claim not statically checkable: a case item is not a constant or the selector is wider than 64 bits",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                          .nextStep = "Check the claim with simulation or formal analysis."});
            return;
        }
        if (overlapRule && chk != UniquePriorityCheck::Priority) {
            // As for LNT-032: an earlier item overlapping under both typings establishes the claim false; else one that depends on it.
            for (size_t j = 1; j < arms.size(); j++) {
                bool both = false, differs = false;
                for (size_t i = 0; i < j && !both; i++) {
                    if (arms[i].group == arms[j].group) continue;
                    // An `inside` set that types differently under the LRM and under slang decides only when both readings agree.
                    const bool first = overlaps(arms[i], arms[j]), second = inside ? overlaps(commonArms[i], commonArms[j]) : first;
                    both = first && second;
                    differs = differs || first != second;
                }
                if (both)
                    l.report(arms[j].expr->sourceRange.start(),
                             "'" + kw + "' claim is statically false: this item matches a value that an earlier item also matches",
                             {.severity = Severity::Error, .nextStep = "Make the items mutually exclusive, or use 'priority case' if first-match semantics are intended."});
                else if (differs)
                    l.report(arms[j].expr->sourceRange.start(),
                             "'" + kw + "' claim not statically checkable: whether the items overlap depends on how the set is typed",
                             {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                              .nextStep = "Give the items and the selector the same signedness, so that every reading agrees."});
            }
        }
        if (overlapRule || chk == UniquePriorityCheck::Unique0 || c.defaultCase) return;
        std::vector<std::string> missing;
        // An `inside` set is read both ways (see LNT-033): a value counts as covered or uncovered only when both readings agree.
        bool typingDecides = false;
        auto uncovered = [&](uint64_t v) {
            const bool a = anyMatch(arms, v), b = inside ? anyMatch(commonArms, v) : a;
            if (a != b) typingDecides = true;
            return !a && !b;
        };
        if (sel.type->isEnum()) {
            for (auto& ev : sel.type->getCanonicalType().as<EnumType>().values()) {
                auto& val = ev.getValue();
                if (!val.isInteger()) continue;
                auto iv = val.integer().as<uint64_t>();
                if (iv && uncovered(*iv)) missing.push_back(std::string(ev.name));
            }
        }
        else if (w <= kMaxEnumeratedWidth) {
            for (uint64_t v = 0; v < (uint64_t(1) << w); v++)
                if (uncovered(v) && missing.size() < 4) missing.push_back(binText(v, w));
        }
        else {
            l.report(c.sourceRange.start(), "'" + kw + "' coverage not checkable: " + std::to_string(w) + "-bit selector is not enumerated",
                     {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .nextStep = "Add a default item or check coverage with formal analysis."});
            return;
        }
        if (typingDecides && missing.empty() && !sel.type->isFourState()) {
            l.report(c.sourceRange.start(), "'" + kw + "' coverage not checkable: whether every value is covered depends on how the set is typed",
                     {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                      .nextStep = "Give the items and the selector the same signedness, or add a default item."});
            return;
        }
        const auto catchAll = [](const std::vector<Arm>& items) {
            return std::any_of(items.begin(), items.end(), [](const Arm& a) {
                return !a.never && !a.range && a.care == 0;
            });
        };
        const bool wildcardCoverage = (c.condition == CaseStatementCondition::WildcardXOrZ && !sel.type->isEnum() && missing.empty()) ||
                                     (c.condition != CaseStatementCondition::Normal && catchAll(arms) &&
                                      (!inside || catchAll(commonArms)));
        // An enum's items name enumerators; an encoding no enumerator has is a value the hardware can still hold.
        if (sel.type->isEnum() && w <= kMaxEnumeratedWidth)
            for (uint64_t v = 0; v < (uint64_t(1) << w) && missing.size() < 4; v++)
                if (uncovered(v)) {
                    bool named = false;
                    for (auto& ev : sel.type->getCanonicalType().as<EnumType>().values())
                        if (ev.getValue().isInteger() && ev.getValue().integer().as<uint64_t>() == v) named = true;
                    if (!named) missing.push_back(binText(v, w) + " (no enumerator)");
                }
        const bool onlyUnknown = missing.empty();
        if (sel.type->isFourState() && !wildcardCoverage) missing.push_back("X/Z");   // unknown selector bits match no non-wildcard item
        if (missing.empty()) return;
        std::string list;
        for (auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        if (typingDecides) list += "; whether other values are covered depends on how the set is typed";
        // Every two-state value has an item and only the unknown bits of a four-state selector are left: those are no hardware value, and
        // a violation on them is the simulation check doing its job.
        if (onlyUnknown && !typingDecides) {
            l.report(c.sourceRange.start(), "'" + kw + "' lists every two-state value; only X/Z of the four-state selector matches no item (no default item)",
                     {.severity = Severity::Info, .nextStep = "No action unless an X selector must be handled; add a default item to define the X/Z case."});
            return;
        }
        l.report(c.sourceRange.start(), "'" + kw + "' claim cannot be satisfied for uncovered value(s) " + list + " (no default item)",
                 {.nextStep = "Add a default item, or cover the listed values."});
    }

    void checkIf(Lint& l, const ConditionalStatement& s) {
        const std::string kw = keyword(s.check) + " if";
        std::vector<const Expression*> conds;
        bool hasElse = true, coverageShape = true;
        const ConditionalStatement* cur = &s;
        for (;;) {
            if (cur->conditions.size() != 1) coverageShape = false;
            for (auto& c : cur->conditions) {
                if (c.pattern) coverageShape = false;
                conds.push_back(c.expr.get());
            }
            const Statement* f = cur->ifFalse;
            if (!f) {
                hasElse = false;
                break;
            }
            if (f->kind != StatementKind::Conditional || f->as<ConditionalStatement>().check != UniquePriorityCheck::None) break;
            cur = &f->as<ConditionalStatement>();
        }
        if (!overlapRule && !hasElse && (s.check == UniquePriorityCheck::Unique || s.check == UniquePriorityCheck::Priority)) {
            // Enumerate only pure expressions of one bounded two-state selector. Calls and
            // assignments must never be executed as part of a coverage proof.
            struct Inputs : ASTVisitor<Inputs, VisitFlags::AllGood> {
                std::set<const ValueSymbol*> symbols;
                bool opaque = false;
                void handle(const NamedValueExpression& e) {
                    if (e.symbol.kind != SymbolKind::Parameter && e.symbol.kind != SymbolKind::EnumValue)
                        symbols.insert(&e.symbol);
                }
                void handle(const CallExpression&) { opaque = true; }
                void handle(const AssignmentExpression&) { opaque = true; }
                void handle(const UnaryExpression& e) {
                    if (e.op == UnaryOperator::Preincrement || e.op == UnaryOperator::Postincrement ||
                        e.op == UnaryOperator::Predecrement || e.op == UnaryOperator::Postdecrement) opaque = true;
                    visitDefault(e);
                }
            } inputs;
            inputs.opaque = !coverageShape;
            bool covered = false;
            for (auto* e : conds) {
                e->visit(inputs);
                if (!inputs.opaque) {
                    auto value = l.evalConst(*e);
                    if (value.isTrue()) covered = true;
                }
            }
            bool decided = !inputs.opaque && inputs.symbols.size() <= 1;
            const ValueSymbol* selector = inputs.symbols.empty() ? nullptr : *inputs.symbols.begin();
            uint32_t width = selector ? selector->getType().getBitWidth() : 0;
            decided = decided && (!selector || (selector->getType().isIntegral() && !selector->getType().isFourState() && width > 0 && width <= kMaxEnumeratedWidth));
            if (!covered && decided) {
                ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
                EvalContext ec(ac);
                ec.pushEmptyFrame();
                ConstantValue* local = selector ? ec.createLocal(selector) : nullptr;
                covered = true;
                for (uint64_t v = 0; v < (uint64_t(1) << width); ++v) {
                    if (local) *local = SVInt(width, v, selector->getType().isSigned());
                    bool matched = false, unknown = false;
                    for (auto* e : conds) {
                        auto value = e->eval(ec);
                        matched |= value.isTrue();
                        unknown |= !value.isTrue() && !value.isFalse();
                    }
                    if (!matched) {
                        covered = false;
                        if (unknown) decided = false;
                    }
                }
            }
            if (!covered)
                l.report(s.sourceRange.start(), "'" + kw + (decided
                         ? "' chain leaves selector values uncovered (no final else)"
                         : "' chain coverage not statically checkable (no final else)"),
                         {.confidence = decided ? Confidence::Established : Confidence::Unresolved,
                          .nextStep = "Add a final else branch, or prove coverage with formal analysis.", .identityKey = "missing-else"});
        }
        // The overlap claim of unique/unique0 belongs to LNT-033 alone.
        if (!overlapRule || (s.check != UniquePriorityCheck::Unique && s.check != UniquePriorityCheck::Unique0)) return;
        int trueCount = 0;
        bool allConst = true;
        std::vector<bool> isConst;
        for (auto* e : conds) {
            auto cv = l.evalConst(*e);
            isConst.push_back(bool(cv));
            if (!cv) allConst = false;
            else if (cv.isTrue()) trueCount++;
        }
        bool identical = false;   // two non-constant conditions that are the same expression: true together or not at all
        for (size_t j = 1; j < conds.size() && !identical; j++)
            for (size_t i = 0; i < j; i++)
                if (!isConst[i] && !isConst[j] && sameCondition(*conds[i], *conds[j])) {
                    identical = true;
                    break;
                }
        if (trueCount >= 2) {
            l.report(s.sourceRange.start(), "'" + kw + "' claim is statically false: " + std::to_string(trueCount) + " conditions are true",
                     {.severity = Severity::Error, .nextStep = "Make the conditions mutually exclusive."});
        }
        else if (identical) {
            l.report(s.sourceRange.start(), "'" + kw + "' claim is statically false: two conditions are the same expression, so they are true together",
                     {.severity = Severity::Error, .nextStep = "Make the conditions mutually exclusive."});
        }
        else if (!allConst) {
            l.report(s.sourceRange.start(), "'" + kw + "' claim not statically checkable: a condition is not a constant",
                     {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .nextStep = "Check the claim with simulation or formal analysis.", .identityKey = "nonconstant-claim"});
        }
    }

    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
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

// slang's case-default (a case statement with no default item) on a statement whose items provably match every two-state value of the
// selector: no value is left without an arm, so the missing default builds nothing different and is style (INFO under the LNT-065
// policy). A casez or casex counts through its wildcard bits. Anything not proven (a non-constant item, a selector wider than 16 bits,
// `inside`, a `matches` case) keeps slang's WARNING.
void demoteCompleteCaseDefaultsImpl(RuleContext& ctx) {
    bool any = false;
    for (const auto& d : ctx.report.diagnostics())
        any = any || (d.ruleId == "LNT-065" && d.origin == "slang" && d.severity == Severity::Warning && d.upstreamCode == "case-default");
    if (!any || !ctx.design.compilation) return;
    Lint l(ctx, "LNT-065");
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        // A location shared by several specialisations of one module is complete only when every specialisation is: the selector's width
        // and the items may depend on a parameter.
        std::set<std::pair<std::string, uint32_t>> complete, notProven;
        explicit V(Lint& lint) : l(lint) {}
        bool provenComplete(const CaseStatement& c) {
            if (c.condition == CaseStatementCondition::Inside || !c.expr.type->isIntegral()) return false;
            const auto& sel = c.expr.unwrapImplicitConversions();
            const uint32_t w = sel.type->getBitWidth();
            if (w == 0 || w > 16) return false;
            std::vector<Arm> arms;
            for (auto& g : c.items)
                for (auto* e : g.expressions) {
                    Arm a;
                    if (!buildArm(l, *e, c.condition, w, sel.type->isSigned(), a)) return false;
                    arms.push_back(a);
                }
            for (uint64_t v = 0; v < (uint64_t(1) << w); v++)
                if (!anyMatch(arms, v)) return false;
            return true;
        }
        void handle(const CaseStatement& c) {
            visitDefault(c);
            if (c.defaultCase || !l.isUser(c.sourceRange.start())) return;
            const auto at = l.loc(c.sourceRange.start());
            (provenComplete(c) ? complete : notProven).insert({at.file, at.line});
        }
    } v(l);
    l.visitHardware(v);
    for (auto& d : ctx.report.mutableDiagnostics()) {
        if (!(d.ruleId == "LNT-065" && d.origin == "slang" && d.severity == Severity::Warning && d.upstreamCode == "case-default")) continue;
        if (!v.complete.count({d.location.file, d.location.line}) || v.notProven.count({d.location.file, d.location.line})) continue;
        d.severity = Severity::Info;
        d.nextStep = "No action unless a default is wanted for style: the items already match every two-state value of the selector.";
        d.applyDefaultDisposition();   // the disposition follows the new severity
    }
}

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
        l.visitHardware(v);
        for (auto& [key, k] : v.constructs)
            if (k.elaborated == 0 && k.total > 0)
                l.report(k.loc, "no generate branch of this construct is elaborated in this configuration",
                         {.severity = Severity::Info, .nextStep = "The construct is dead for the selected parameters; check the parameter values.", .identityKey = "construct"});
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-078: a declaration that shadows another. slang reports the shadowing of an outer declaration (shadow-value, shadow-hierarchy,
// shadow-property); the name a wildcard import (`import p::*;`) makes visible is not one it reports, so a local parameter,
// variable, net, typedef, enumerator or subroutine formal that hides it is found here.
class ShadowRule final : public LintRule {
public:
    ShadowRule() : LintRule("LNT-078", "", true) {}

private:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void inspect(const Scope& scope) {
            std::vector<const PackageSymbol*> packages;   // every wildcard import in force here, nearest scope first
            for (const Scope* s = &scope; s; s = s->asSymbol().getParentScope())
                for (const auto& member : s->members())
                    if (member.kind == SymbolKind::WildcardImport)
                        if (const auto* package = member.as<WildcardImportSymbol>().getPackage()) packages.push_back(package);
            if (packages.empty()) return;
            for (const auto& member : scope.members()) {
                switch (member.kind) {
                    case SymbolKind::Parameter:
                    case SymbolKind::TypeAlias:
                    case SymbolKind::EnumValue:
                    case SymbolKind::Variable:
                    case SymbolKind::FormalArgument:   // a subroutine's formal
                    case SymbolKind::Net: break;
                    case SymbolKind::TransparentMember:   // an enumerator is exposed in the scope of its type
                        if (member.as<TransparentMemberSymbol>().wrapped.kind == SymbolKind::EnumValue) break;
                        continue;
                    default: continue;
                }
                if (member.name.empty() || !l.isUser(member.location)) continue;
                for (const auto* package : packages) {
                    if (!package->findForImport(member.name)) continue;
                    l.count();
                    const std::string name(member.name);
                    l.report(member, "declaration '" + name + "' hides the '" + name + "' that the wildcard import of package '" +
                                         std::string(package->name) + "' makes visible",
                             {.severity = Severity::Info, .confidence = Confidence::Established,
                              .nextStep = "Rename the local declaration, or import the package item explicitly.",
                              .signals = {name}, .identityKey = "import:" + std::string(package->name)});
                    break;
                }
            }
        }
        void handle(const InstanceBodySymbol& b) { inspect(b); visitDefault(b); }
        void handle(const GenerateBlockSymbol& b) { inspect(b); visitDefault(b); }
        void handle(const StatementBlockSymbol& b) { inspect(b); visitDefault(b); }
        void handle(const SubroutineSymbol& b) { inspect(b); visitDefault(b); }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

} // namespace

void demoteCompleteCaseDefaults(RuleContext& ctx) { demoteCompleteCaseDefaultsImpl(ctx); }

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
    v.push_back(std::make_unique<ShadowRule>());
    return v;
}

} // namespace sverity
