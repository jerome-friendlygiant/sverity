// Native AST lint: synthesizability. LNT-019 non-synthesizable constructs, LNT-061 system tasks, reset and clock-event
// coding (LNT-020 reset coverage, LNT-022 edge mixing, LNT-047 data-driven events, LNT-058 async reset polarity),
// and X/Z hazards (LNT-039 X/Z assignments, LNT-040 X optimism/pessimism). Helpers and template: lint_procedural.cpp.
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "rules/rules.h"
#include "rules/lint/loop_facts.h"
#include "rules/lint/write_ranges.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/statements/MiscStatements.h"
#include "domains/cone.h"
#include "domains/model.h"
#include "sverity/lint.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/ClassSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/CoverSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/numeric/SVInt.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;
using slang::analysis::ValueDriver;

// ---------------------------------------------------------------------------------------------------------------
// Shared helpers

/// Sets a value for the lifetime of a scope and restores the previous one.
template <typename T>
struct Scoped {
    T& ref;
    T old;
    Scoped(T& r, T v) : ref(r), old(r) { ref = v; }
    ~Scoped() { ref = old; }
};

/// Drops the implicit conversions slang inserts around operands.
const Expression& strip(const Expression& e) {
    const Expression* p = &e;
    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit())
        p = &p->as<ConversionExpression>().operand();
    return *p;
}

/// True for a literal carrying x or z bits; hasX / hasZ tell which kinds it carries.
bool xzLiteral(const Expression& e, bool& hasX, bool& hasZ) {
    hasX = hasZ = false;
    if (e.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral) {
        auto bit = e.as<UnbasedUnsizedIntegerLiteral>().getLiteralValue();
        hasX = bit.value == logic_t::x.value;
        hasZ = bit.value == logic_t::z.value;
    }
    else if (e.kind == ExpressionKind::IntegerLiteral) {
        auto v = e.as<IntegerLiteral>().getValue();
        if (v.hasUnknown())
            for (uint32_t i = 0; i < v.getBitWidth(); i++) {
                auto bit = v[i];
                if (bit.isUnknown()) (bit.value == logic_t::z.value ? hasZ : hasX) = true;
            }
    }
    return hasX || hasZ;
}

// True when the low `keep` bits of `v` are all z: only then is the value a tri-state on every bit it drives. A literal
// such as 2'bz0 also drives a 0 bit, which can contend with another driver.
bool allZ(const SVInt& v, uint32_t keep = UINT32_MAX) {
    const uint32_t n = std::min(keep, v.getBitWidth());
    if (n == 0) return false;
    for (uint32_t i = 0; i < n; i++)
        if (v[i].value != logic_t::z.value) return false;
    return true;
}

bool isXZ(const Expression& e) {
    bool x, z;
    return xzLiteral(strip(e), x, z);
}

/// A literal used as a single bit (an if or ?: condition).
bool oneBitLiteral(const Expression& e) {
    return e.kind != ExpressionKind::IntegerLiteral || e.as<IntegerLiteral>().getValue().getBitWidth() == 1;
}

/// Pre-order search for the first sub-expression satisfying `pred`.
template <typename Pred>
struct ExprFinder : ASTVisitor<ExprFinder<Pred>, VisitFlags::AllGood> {
    Pred pred;
    const Expression* hit = nullptr;
    explicit ExprFinder(Pred p) : pred(std::move(p)) {}
    template <typename T>
        requires std::derived_from<T, Expression>
    void handle(const T& e) {
        if (!hit && pred(static_cast<const Expression&>(e))) hit = &e;
        if (!hit) this->visitDefault(e);
    }
};

template <typename Pred>
const Expression* findExpr(const Expression& e, Pred pred) {
    ExprFinder<Pred> f(std::move(pred));
    e.visit(f);
    return f.hit;
}

/// First sub-expression of a statement satisfying `pred`.
template <typename Pred>
const Expression* findExprIn(const Statement& st, Pred pred) {
    ExprFinder<Pred> f(std::move(pred));
    st.visit(f);
    return f.hit;
}

bool hasUserCall(const Expression& e) {
    return findExpr(e, [](const Expression& x) {
               return x.kind == ExpressionKind::Call && !x.as<CallExpression>().isSystemCall();
           }) != nullptr;
}

/// The signal an expression names (a plain name or a select of one), or null.
const Symbol* symOf(const Expression& e) {
    const Expression& s = strip(e);
    if (s.kind == ExpressionKind::NamedValue) return &s.as<NamedValueExpression>().symbol;
    return s.getSymbolReference();
}

/// The control an expression names, as the path from its root signal through struct / interface members and constant selects
/// (`c.rst_n` is {c, rst_n}, `r[1]` is {r, [1]}). Distinct bits or elements are distinct controls. A select whose index is not
/// a constant is keyed by the index signal when it is one (the same `i` is the same control), and otherwise matches nothing.
/// Empty when the expression is not such a reference.
struct CtlStep {
    const Symbol* sym = nullptr;   // a signal or member; null for a select
    std::string sel;               // a select: "[3]", "[3:1]", "[i#<symbol>]", "[?#<expression>]"
    bool operator==(const CtlStep&) const = default;
};
using Ctl = std::vector<CtlStep>;

std::string selKey(Lint& l, const Expression& e) {
    const Expression& s = strip(e);
    auto cv = l.evalConst(s);
    if (cv.isInteger() && !cv.integer().hasUnknown()) return cv.integer().toString();
    if (s.kind == ExpressionKind::NamedValue) return "#" + std::to_string(reinterpret_cast<uintptr_t>(&s.as<NamedValueExpression>().symbol));
    return "?" + std::to_string(reinterpret_cast<uintptr_t>(&s));
}

Ctl ctlOf(Lint& l, const Expression& e0) {
    const Expression& e = strip(e0);
    switch (e.kind) {
        case ExpressionKind::NamedValue:
        case ExpressionKind::HierarchicalValue: return {{&e.as<ValueExpressionBase>().symbol, {}}};
        case ExpressionKind::ElementSelect: {
            Ctl c = ctlOf(l, e.as<ElementSelectExpression>().value());
            if (!c.empty()) c.push_back({nullptr, "[" + selKey(l, e.as<ElementSelectExpression>().selector()) + "]"});
            return c;
        }
        case ExpressionKind::RangeSelect: {
            auto& r = e.as<RangeSelectExpression>();
            Ctl c = ctlOf(l, r.value());
            if (!c.empty()) {
                const char* kind = r.getSelectionKind() == RangeSelectionKind::Simple ? ":" : r.getSelectionKind() == RangeSelectionKind::IndexedUp ? "+:" : "-:";
                c.push_back({nullptr, "[" + selKey(l, r.left()) + kind + selKey(l, r.right()) + "]"});
            }
            return c;
        }
        case ExpressionKind::MemberAccess: {
            Ctl c = ctlOf(l, e.as<MemberAccessExpression>().value());
            if (!c.empty()) c.push_back({&e.as<MemberAccessExpression>().member, {}});
            return c;
        }
        default: return {};
    }
}

/// The same path with its selects dropped: the signal (and members) a control belongs to.
Ctl ctlRoot(const Ctl& c) {
    Ctl r;
    for (auto& st : c)
        if (st.sym) r.push_back(st);
    return r;
}

std::string ctlName(const Ctl& c) {
    std::string n;
    for (auto& st : c) {
        if (!st.sym) n += st.sel.find_first_of("#?") == std::string::npos ? st.sel : "[...]";
        else n += (n.empty() ? "" : ".") + std::string(st.sym->name);
    }
    return n.empty() ? std::string("<expression>") : n;
}

/// A name, member access or select of one: the forms a test of a control can take directly.
bool isDirectRef(const Expression& e) {
    return e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue || e.kind == ExpressionKind::MemberAccess ||
           e.kind == ExpressionKind::ElementSelect || e.kind == ExpressionKind::RangeSelect;
}

/// Whether a reference with path `ref` can read the control `c`: the same path, a whole signal or struct that contains it, or a
/// select that may reach the same element (a range, or an index that is not a constant). Distinct constant selects (`r[0]`, `r[1]`)
/// are distinct controls.
bool mayRead(const Ctl& ref, const Ctl& c) {
    for (size_t i = 0; i < ref.size() && i < c.size(); i++) {
        if (ref[i] == c[i]) continue;
        auto constantIndex = [](const CtlStep& st) { return !st.sym && st.sel.find_first_not_of("[]0123456789-") == std::string::npos; };
        if (!ref[i].sym && !c[i].sym && !(constantIndex(ref[i]) && constantIndex(c[i]))) continue;
        return false;
    }
    return true;
}

const Expression* findCtl(Lint& l, const Expression& e, const Ctl& c) {
    if (c.empty()) return nullptr;
    return findExpr(e, [&](const Expression& x) { return isDirectRef(x) && ctlOf(l, x) == c; });
}

/// A plain signal reference: a name, or a select / member access of one. No operators.
bool isPlain(const Expression& e0) {
    const Expression& e = strip(e0);
    switch (e.kind) {
        case ExpressionKind::NamedValue:
        case ExpressionKind::HierarchicalValue: return true;
        case ExpressionKind::ElementSelect: return isPlain(e.as<ElementSelectExpression>().value());
        case ExpressionKind::RangeSelect: return isPlain(e.as<RangeSelectExpression>().value());
        case ExpressionKind::MemberAccess: return isPlain(e.as<MemberAccessExpression>().value());
        default: return false;
    }
}

std::string lowerName(const Symbol* s) {
    std::string n = s ? std::string(s->name) : std::string();
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return n;
}
bool clockLike(const Symbol* s) {
    auto n = lowerName(s);
    return n.find("clk") != std::string::npos || n.find("clock") != std::string::npos;
}

/// Strips a statement down to the statement it wraps: begin/end and the first real statement of a list.
const Statement* unwrap(const Statement* s) {
    while (s) {
        if (s->kind == StatementKind::Block) {
            s = &s->as<BlockStatement>().body;
        }
        else if (s->kind == StatementKind::List) {
            const Statement* next = nullptr;
            for (auto* c : s->as<StatementList>().list)
                if (c && c->kind != StatementKind::VariableDeclaration && c->kind != StatementKind::Empty) {
                    next = c;
                    break;
                }
            s = next;
        }
        else {
            return s;
        }
    }
    return nullptr;
}

const ConditionalStatement* asIf(const Statement* s) {
    s = unwrap(s);
    return s && s->kind == StatementKind::Conditional ? &s->as<ConditionalStatement>() : nullptr;
}

std::vector<const AssignmentExpression*> assignsIn(const Statement* s) {
    Assigns a;
    if (s) s->visit(a);
    return a.list;
}

/// Calls `fn` once per procedural block of the design (canonical instance bodies only).
template <typename F>
struct ProcWalk : HardwareVisitor<ProcWalk<F>, VisitFlags::AllCanonical> {
    F fn;
    explicit ProcWalk(F f) : fn(std::move(f)) {}
    void handle(const ProceduralBlockSymbol& p) { fn(p); }
};

template <typename F>
void forEachProc(Lint& l, F fn) {
    ProcWalk<F> w(std::move(fn));
    l.visitHardware(w);
}

// ---------------------------------------------------------------------------------------------------------------
// Sequential view: the edge-sensitive controls of an always block, which control is the clock and which is an
// asynchronous reset, and the first `if` the block starts with.

void collectEdges(const TimingControl& t, std::vector<const SignalEventControl*>& out) {
    switch (t.kind) {
        case TimingControlKind::SignalEvent:
            if (auto& s = t.as<SignalEventControl>(); s.edge != EdgeKind::None) out.push_back(&s);
            break;
        case TimingControlKind::EventList:
            for (auto* e : t.as<EventListControl>().events) collectEdges(*e, out);
            break;
        default: break;
    }
}

/// Inversion parity of a direct test of `s` in `e` (`s` -> 0, `!s` -> 1, `s == 0` -> 1, `s != 1` -> 1, ...).
/// -1 when the expression is not a direct test of `s`.
int testPolarity(Lint& l, const Expression& e0, const Ctl& s, bool compound = true) {
    const Expression& e = strip(e0);
    if (isDirectRef(e)) return ctlOf(l, e) == s ? 0 : -1;
    if (e.kind == ExpressionKind::UnaryOp && (e.as<UnaryExpression>().op == UnaryOperator::LogicalNot ||
                                              (e.as<UnaryExpression>().op == UnaryOperator::BitwiseNot && e.type->getBitWidth() == 1))) {
        int inner = testPolarity(l, e.as<UnaryExpression>().operand(), s, compound);
        return inner < 0 ? -1 : inner ^ 1;
    }
    if (e.kind == ExpressionKind::BinaryOp) {
        auto& b = e.as<BinaryExpression>();
        if (!compound && (b.op == BinaryOperator::LogicalOr || b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryAnd)) return -1;
        if (b.op == BinaryOperator::LogicalOr || b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryAnd) {
            // compound test: the operand that tests `s` decides the polarity (both testing it with different parity -> undecidable)
            int a = testPolarity(l, b.left(), s, compound), c = testPolarity(l, b.right(), s, compound);
            if (a >= 0 && c >= 0 && a != c) return -1;
            return a >= 0 ? a : c;
        }
        const bool equal = b.op == BinaryOperator::Equality || b.op == BinaryOperator::CaseEquality;
        if (!equal && b.op != BinaryOperator::Inequality && b.op != BinaryOperator::CaseInequality) return -1;
        const Expression& lhs = strip(b.left());
        const Expression& rhs = strip(b.right());
        bool swapped = !isDirectRef(lhs);
        const Expression& ref = swapped ? rhs : lhs;
        const Expression& cst = swapped ? lhs : rhs;
        if (!isDirectRef(ref) || ctlOf(l, ref) != s) return -1;
        auto cv = l.evalConst(cst);
        if (!cv.isInteger()) return -1;
        auto c = cv.integer().as<uint64_t>();
        if (!c || *c > 1) return -1;
        bool inverted = equal ? *c == 0 : *c == 1;
        return inverted ? 1 : 0;
    }
    return -1;
}

/// The truth of a reset test while the control is at `level`, with every other signal unknown: constants in the test are
/// evaluated, so a term such as `&& EN` with EN = 0 decides the test whatever the signals do.
enum class Tri { No, Yes, Unknown };
Tri triNot(Tri t) { return t == Tri::Unknown ? t : t == Tri::Yes ? Tri::No : Tri::Yes; }
Tri testValue(Lint& l, const Expression& e0, const Ctl& s, int level) {
    const Expression& e = strip(e0);
    auto cv = l.evalConst(e);
    if (cv.isInteger()) return cv.integer().hasUnknown() ? Tri::Unknown : cv.isTrue() ? Tri::Yes : Tri::No;
    if (isDirectRef(e)) return ctlOf(l, e) == s && e.type->getBitWidth() == 1 ? (level ? Tri::Yes : Tri::No) : Tri::Unknown;
    if (e.kind == ExpressionKind::UnaryOp) {
        auto& u = e.as<UnaryExpression>();
        if (u.op == UnaryOperator::LogicalNot || (u.op == UnaryOperator::BitwiseNot && e.type->getBitWidth() == 1))
            return triNot(testValue(l, u.operand(), s, level));
    }
    if (e.kind == ExpressionKind::BinaryOp) {
        auto& b = e.as<BinaryExpression>();
        const bool bit = e.type->getBitWidth() == 1;
        const bool isAnd = b.op == BinaryOperator::LogicalAnd || (bit && b.op == BinaryOperator::BinaryAnd);
        const bool isOr = b.op == BinaryOperator::LogicalOr || (bit && b.op == BinaryOperator::BinaryOr);
        if (isAnd || isOr) {
            const Tri x = testValue(l, b.left(), s, level), y = testValue(l, b.right(), s, level);
            const Tri decisive = isAnd ? Tri::No : Tri::Yes;
            if (x == decisive || y == decisive) return decisive;
            return x == Tri::Unknown || y == Tri::Unknown ? Tri::Unknown : triNot(decisive);
        }
        if (b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseEquality ||
            b.op == BinaryOperator::CaseInequality) {
            const int pol = testPolarity(l, e, s, false);   // a direct test of the control against 0 or 1
            if (pol >= 0) return (pol == 1) == (level == 0) ? Tri::Yes : Tri::No;
        }
    }
    return Tri::Unknown;
}

/// Why the test cannot be a real asynchronous control of `s` (an edge of `edge`), or empty: constants in it make it true or false
/// at both levels of the control, so the branch it selects never runs, or runs whatever the control does.
std::string constantTestProblem(Lint& l, const Expression& test, const Ctl& s, EdgeKind edge) {
    const int active = edge == EdgeKind::NegEdge ? 0 : 1;
    const Tri atActive = testValue(l, test, s, active), atInactive = testValue(l, test, s, 1 - active);
    if (atActive == Tri::No && atInactive == Tri::No) return "is false at both levels of '" + ctlName(s) + "' (a constant term decides it), so the branch it selects never runs";
    if (atActive == Tri::Yes && atInactive == Tri::Yes) return "is true at both levels of '" + ctlName(s) + "' (a constant term decides it), so the branch it selects runs whatever the control does";
    return {};
}

struct SeqView {
    const ProceduralBlockSymbol* proc = nullptr;
    const Statement* body = nullptr;                 // statement controlled by the event list
    std::vector<const SignalEventControl*> edges;    // edge-sensitive controls, source order
    std::vector<Ctl> ctl;                            // edge i's control (empty: not a signal reference)
    std::vector<bool> tested;                        // edge i's signal is referenced by the first if
    Ctl resetCtl;                                    // the identified reset's control
    const ConditionalStatement* first = nullptr;     // the first statement, when it is an if
    const SignalEventControl* clock = nullptr;
    const SignalEventControl* reset = nullptr;       // asynchronous reset control, when identified
    bool heuristicReset = false;                     // reset chosen by name; the first if does not test it
    bool ambiguous = false;                          // several async controls, or no classification possible
    int polarity = -1;                               // inversions of the reset test in the first if (-1: none)
    // Several asynchronous controls (set and reset): the branches of the first if / else-if chain that run while one is asserted,
    // in priority order, and the statement that runs when none is. Filled only when each of n-1 edges is tested directly in its own
    // chain element with its edge's polarity and the remaining edge is the clock; `ambiguous` stays set for the other rules.
    std::vector<const Statement*> chain;
    std::vector<Ctl> chainCtl;                       // the control each chain branch belongs to
    size_t chainClock = 0;                           // the edge that is no control: the clock
    const Statement* chainOther = nullptr;

    bool activeOnTrue() const {  // reset is active when the first-if condition is true
        return ((polarity & 1) == 1) == (reset->edge == EdgeKind::NegEdge);
    }
};

std::optional<SeqView> seqView(Lint& l, const ProceduralBlockSymbol& p) {
    if (p.procedureKind != ProceduralBlockKind::Always && p.procedureKind != ProceduralBlockKind::AlwaysFF)
        return std::nullopt;
    const Statement& body = p.getBody();
    if (body.kind != StatementKind::Timed) return std::nullopt;
    auto& t = body.as<TimedStatement>();
    SeqView v;
    v.proc = &p;
    v.body = &t.stmt;
    collectEdges(t.timing, v.edges);
    if (v.edges.empty()) return std::nullopt;
    v.first = asIf(v.body);
    const size_t n = v.edges.size();
    v.tested.assign(n, false);
    for (auto* e : v.edges) v.ctl.push_back(ctlOf(l, e->expr));
    // Recognition looks at the first if and its else-if chain (the same statement in source terms).
    for (const ConditionalStatement* c = v.first; c; c = asIf(c->ifFalse))
        for (size_t i = 0; i < n; i++) {
            for (auto& cond : c->conditions)
                if (findCtl(l, *cond.expr, v.ctl[i])) v.tested[i] = true;
        }
    size_t nTested = static_cast<size_t>(std::count(v.tested.begin(), v.tested.end(), true));
    size_t idx = static_cast<size_t>(std::find(v.tested.begin(), v.tested.end(), true) - v.tested.begin());
    if (n == 1) {
        v.clock = v.edges[0];
    }
    else if (n == 2 && nTested == 1) {
        v.reset = v.edges[idx];
        v.clock = v.edges[1 - idx];
    }
    else if (n == 2 && nTested == 0) {
        bool c0 = clockLike(symOf(v.edges[0]->expr)), c1 = clockLike(symOf(v.edges[1]->expr));
        if (c0 != c1) {
            size_t r = c0 ? 1 : 0;
            v.reset = v.edges[r];
            v.clock = v.edges[1 - r];
            v.heuristicReset = true;
        }
        else {
            v.ambiguous = true;
        }
    }
    else {
        v.ambiguous = true;
        if (n >= 3 && nTested == n - 1) {
            std::vector<bool> used(n, false);
            const Statement* rest = v.body;
            bool ok = true;
            for (const ConditionalStatement* c = v.first; c && ok;) {
                size_t hit = n, hits = 0;
                for (size_t i = 0; i < n; i++)
                    if (v.tested[i] && c->conditions.size() == 1 && !c->conditions[0].pattern && findCtl(l, *c->conditions[0].expr, v.ctl[i])) { hit = i; hits++; }
                if (hits != 1 || used[hit]) break;
                const int pol = testPolarity(l, *c->conditions[0].expr, v.ctl[hit], false);
                if (pol < 0 || ((pol & 1) == 1) != (v.edges[hit]->edge == EdgeKind::NegEdge)) { ok = false; break; }
                used[hit] = true;
                v.chain.push_back(&c->ifTrue);
                v.chainCtl.push_back(v.ctl[hit]);
                rest = c->ifFalse;
                c = asIf(c->ifFalse);
            }
            if (!ok || v.chain.size() != n - 1) { v.chain.clear(); v.chainCtl.clear(); }
            else {
                v.chainOther = rest;
                v.chainClock = static_cast<size_t>(std::find(used.begin(), used.end(), false) - used.begin());
            }
        }
    }
    if (v.reset && v.first) {
        v.resetCtl = v.ctl[static_cast<size_t>(std::find(v.edges.begin(), v.edges.end(), v.reset) - v.edges.begin())];
        if (!v.resetCtl.empty()) v.polarity = testPolarity(l, *v.first->conditions[0].expr, v.resetCtl);
    }
    return v;
}

/// The statements run while the reset is active (`reset`) and while it is not (`other`).
struct Branches {
    const Statement* reset = nullptr;
    const Statement* other = nullptr;
    bool exact = false;  // the first if decides the branch (direct test of the reset); otherwise a convention
};

Branches branchesOf(const SeqView& v) {
    Branches b;
    if (!v.reset || !v.first) return b;
    b.exact = !v.heuristicReset && v.polarity >= 0;
    bool resetOnTrue = b.exact ? v.activeOnTrue() : true;   // unmatched or compound test: assume `if (rst)`
    b.reset = resetOnTrue ? &v.first->ifTrue : v.first->ifFalse;
    b.other = resetOnTrue ? v.first->ifFalse : &v.first->ifTrue;
    return b;
}

// The block scopes written inside a final block, nested ones included. A declaration in such a scope is visited from the scope
// (an instance-body member), not from the procedure, so the visitor needs these scopes before it walks the design.
struct FinalScopes : HardwareVisitor<FinalScopes, VisitFlags::AllCanonical> {
    std::set<const Symbol*> scopes;
    void handle(const BlockStatement& b) {
        if (b.blockSymbol) scopes.insert(b.blockSymbol);
        this->visitDefault(b);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-019: constructs that have no synthesizable meaning. Final blocks are advisory (INFO); assertions are exempt.
class NonSynthRule final : public LintRule {
public:
    enum class Family { Timing, ProceduralOverride, DataType };
    explicit NonSynthRule(Family family) : LintRule(family == Family::Timing ? "LNT-019" : family == Family::ProceduralOverride ? "LNT-079" : "LNT-080"), family(family) {}

private:
    Family family;
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        Family family;
        bool fpga;
        bool inAssert = false;
        bool testbench = false;   // inside a top-level instance without ports: a testbench-like unit, findings are INFO
        bool inFinal = false;     // lexically inside a final block (its body, macro-written or not); set by the visitor, not by location
        std::set<const Symbol*> finalScopes;   // block scopes of final blocks (see FinalScopes)
        bool inFinalScope(const Symbol& s) const {
            for (const Scope* sc = s.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
                if (finalScopes.count(&sc->asSymbol())) return true;
            return false;
        }
        V(Lint& lint, bool isFpga, Family ruleFamily) : l(lint), family(ruleFamily), fpga(isFpga) {}

        void emit(Family owner, slang::SourceLocation at, const std::string& what, Severity base = Severity::Warning,
                  Confidence conf = Confidence::Established) {
            if (family != owner || inAssert || !l.isUser(at)) return;
            l.count();
            const char* where = testbench ? "non-synthesizable construct in a testbench-like top (no ports): "
                                : inFinal ? "non-synthesizable construct in a final block: "
                                          : "non-synthesizable construct in design RTL: ";
            l.report(at, where + what,
                     {.severity = conf == Confidence::Unresolved ? Severity::Warning : (inFinal || testbench ? Severity::Info : base),
                      .confidence = conf,
                      .nextStep = "Move testbench-only constructs out of synthesizable modules, or guard them."});
        }
        static bool delayCtl(const TimingControl& t) {
            return t.kind == TimingControlKind::Delay || t.kind == TimingControlKind::Delay3 ||
                   t.kind == TimingControlKind::OneStepDelay;
        }
        /// A loop bound is static when the expression is a constant, or a relational test of a name against one.
        bool staticBound(const Expression& e, bool forLoop) {
            if (static_cast<bool>(l.evalConst(e))) return true;
            if (!forLoop) return false;
            const Expression& s = strip(e);
            if (s.kind != ExpressionKind::BinaryOp) return false;
            auto& b = s.as<BinaryExpression>();
            switch (b.op) {
                case BinaryOperator::LessThan:
                case BinaryOperator::LessThanEqual:
                case BinaryOperator::GreaterThan:
                case BinaryOperator::GreaterThanEqual:
                case BinaryOperator::Inequality: break;
                default: return false;
            }
            const Expression& lhs = strip(b.left());
            const Expression& rhs = strip(b.right());
            return (lhs.kind == ExpressionKind::NamedValue && static_cast<bool>(l.evalConst(rhs))) ||
                   (rhs.kind == ExpressionKind::NamedValue && static_cast<bool>(l.evalConst(lhs)));
        }
        void checkBound(const Expression& e, bool forLoop, const std::string& what) {
            if (staticBound(e, forLoop)) return;
            emit(Family::Timing, e.sourceRange.start(), what + " is not a compile-time constant; Unresolved: synthesis support is not proven", Severity::Warning,
                 Confidence::Unresolved);
        }

        // Testbench-like: a top-level instance without ports, and a portless instance inside one. A ported instance is design
        // RTL wherever it sits, so a dut with ports under a portless top keeps WARNING.
        void handle(const InstanceSymbol& inst) {
            bool saved = testbench;
            bool topLevel = inst.getParentScope() && inst.getParentScope()->asSymbol().kind == SymbolKind::Root;
            testbench = inst.body.getPortList().empty() && (topLevel || saved);
            this->visitDefault(inst);
            testbench = saved;
        }
        void handle(const ProceduralBlockSymbol& p) {
            // Plain initial assignments are profile-aware LNT-048 assumptions; timing and loops are still checked here. A final block's body is
            // advisory (INFO) wherever its text came from: a macro that writes a final block and an always block marks only
            // the final's body.
            Scoped<bool> f(inFinal, inFinal || p.procedureKind == ProceduralBlockKind::Final);
            this->visitDefault(p);
        }
        void handle(const ContinuousAssignSymbol& ca) {
            if (ca.getDelay()) emit(Family::Timing, ca.location, "'#' delay on a continuous assignment");
            this->visitDefault(ca);
        }
        void handle(const TimedStatement& t) {
            if (delayCtl(t.timing)) emit(Family::Timing, t.sourceRange.start(), "'#' delay");
            this->visitDefault(t);
        }
        void handle(const AssignmentExpression& a) {
            if (a.timingControl && delayCtl(*a.timingControl)) emit(Family::Timing, a.sourceRange.start(), "intra-assignment '#' delay");
            this->visitDefault(a);
        }
        void handle(const WaitStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "'wait' statement");
            this->visitDefault(s);
        }
        void handle(const WaitForkStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "'wait fork'");
            this->visitDefault(s);
        }
        void handle(const WaitOrderStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "'wait_order'");
            this->visitDefault(s);
        }
        void handle(const DisableForkStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "'disable fork'");
            this->visitDefault(s);
        }
        void handle(const BlockStatement& b) {
            if (b.blockKind != StatementBlockKind::Sequential) emit(Family::Timing, b.sourceRange.start(), "fork/join");
            this->visitDefault(b);
        }
        void handle(const EventTriggerStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "event trigger ('->')");
            this->visitDefault(s);
        }
        void handle(const ProceduralAssignStatement& s) {
            emit(Family::ProceduralOverride, s.sourceRange.start(), s.isForce ? "'force'" : "procedural 'assign'");
            this->visitDefault(s);
        }
        void handle(const ProceduralDeassignStatement& s) {
            emit(Family::ProceduralOverride, s.sourceRange.start(), s.isRelease ? "'release'" : "'deassign'");
            this->visitDefault(s);
        }
        void handle(const ForeverLoopStatement& s) {
            emit(Family::Timing, s.sourceRange.start(), "'forever' loop has no static bound");
            this->visitDefault(s);
        }
        void handle(const RepeatLoopStatement& s) {
            checkBound(s.count, false, "'repeat' count");
            this->visitDefault(s);
        }
        void handle(const WhileLoopStatement& s) {
            auto value = l.evalConst(s.cond);
            if (value.isInteger() && !value.integer().hasUnknown() && value.integer().reductionOr().value == 1)
                emit(Family::Timing, s.sourceRange.start(), "constant-true 'while' loop has no proven terminating bound", Severity::Warning, Confidence::Unresolved);
            else checkBound(s.cond, false, "'while' condition");
            this->visitDefault(s);
        }
        void handle(const DoWhileLoopStatement& s) {
            auto value = l.evalConst(s.cond);
            if (value.isInteger() && !value.integer().hasUnknown() && value.integer().reductionOr().value == 1)
                emit(Family::Timing, s.sourceRange.start(), "constant-true 'do-while' loop has no proven terminating bound", Severity::Warning, Confidence::Unresolved);
            else checkBound(s.cond, false, "'do-while' condition");
            this->visitDefault(s);
        }
        void handle(const ForLoopStatement& s) {
            if (!s.stopExpr) emit(Family::Timing, s.sourceRange.start(), "'for' loop without a condition");
            else if (!constantLoopValues(s, l) && !neverRuns(s, l))
                emit(Family::Timing, s.sourceRange.start(), "Unresolved: 'for' loop termination and synthesis support are not proven", Severity::Warning, Confidence::Unresolved);
            this->visitDefault(s);
        }
        void handle(const ImmediateAssertionStatement& s) {
            Scoped<bool> a(inAssert, true);
            this->visitDefault(s);
        }
        void handle(const ConcurrentAssertionStatement& s) {
            Scoped<bool> a(inAssert, true);
            this->visitDefault(s);
        }
        void handle(const InstanceBodySymbol& b) {
            if (b.getDefinition().definitionKind == DefinitionKind::Program) emit(Family::DataType, b.location, "'program' block");
            this->visitDefault(b);
        }
        void handle(const ClassType& c) {
            emit(Family::DataType, c.location, "class declaration");
            this->visitDefault(c);
        }
        void handle(const CovergroupType& c) {
            emit(Family::DataType, c.location, "covergroup declaration");
            this->visitDefault(c);
        }
        // Foreign (DPI) subroutines: synthesis cannot build their body, so what they compute is neither hardware nor checked.
        // A declaration counts where design code declares or calls it; one finding per subroutine, at its declaration.
        struct Foreign { bool design = false; std::vector<slang::SourceLocation> calls; };
        std::map<const SubroutineSymbol*, Foreign> foreign;
        std::vector<const SubroutineSymbol*> foreignOrder;
        Foreign& foreignOf(const SubroutineSymbol& s) {
            auto [it, fresh] = foreign.try_emplace(&s);
            if (fresh) foreignOrder.push_back(&s);
            return it->second;
        }
        bool designUse(slang::SourceLocation) const { return !inAssert && !testbench && !inFinal; }
        void handle(const SubroutineSymbol& s) {
            if (s.flags.has(MethodFlags::DPIImport) && s.getParentScope() &&
                s.getParentScope()->asSymbol().kind == SymbolKind::InstanceBody && l.isUser(s.location))
                foreignOf(s).design |= designUse(s.location);
            this->visitDefault(s);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall())
                if (auto* sub = std::get<const SubroutineSymbol*>(c.subroutine); sub && sub->flags.has(MethodFlags::DPIImport) && l.isUser(c.sourceRange.start())) {
                    auto& f = foreignOf(*sub);
                    f.design |= designUse(c.sourceRange.start());
                    f.calls.push_back(c.sourceRange.start());
                }
            this->visitDefault(c);
        }
        void reportForeign() {
            if (family != Family::DataType) return;
            for (auto* s : foreignOrder) {
                const Foreign& f = foreign.at(s);
                l.count();
                const std::string kind = s->subroutineKind == SubroutineKind::Task ? "task" : "function";
                const std::string mode = s->flags.has(MethodFlags::DPIContext) ? "context " : s->flags.has(MethodFlags::Pure) ? "pure " : "";
                std::vector<Evidence> calls;
                for (auto at : f.calls) {
                    if (calls.size() == 8) break;
                    calls.push_back({"foreign_call", "the result of '" + std::string(s->name) + "' is not modelled here", l.loc(at)});
                }
                l.report(s->location, std::string(f.design ? "non-synthesizable construct in design RTL: " : "non-synthesizable construct outside design logic: ") +
                                          "foreign DPI " + mode + kind + " import '" + std::string(s->name) + "' (its body is not hardware and is not analysed" +
                                          (f.calls.empty() ? "" : "; " + std::to_string(f.calls.size()) + " call(s)") + ")",
                         {.severity = f.design ? Severity::Warning : Severity::Info,
                          .nextStep = "Replace the foreign call with synthesizable RTL, or keep it in testbench-only code.",
                          .evidence = std::move(calls)});
            }
        }
        void handle(const VariableSymbol& v) {
            if (auto* sc = v.getParentScope(); sc && sc->asSymbol().kind == SymbolKind::ClassType) return;  // class property
            Scoped<bool> f(inFinal, inFinal || inFinalScope(v));   // a block-local declaration of a final block
            const Type& t = v.getType();
            std::set<const Type*> visited;
            std::function<const char*(const Type&)> classifyType = [&](const Type& type) -> const char* {
                const auto& t = type.getCanonicalType();
                if (!visited.insert(&t).second) return nullptr;
                if (t.isFloating()) return "real/shortreal variable";
                if (t.isString()) return "string variable";
                if (t.kind == SymbolKind::CHandleType) return "chandle variable";
                if (t.isEvent()) return "event variable";
                if (t.isVirtualInterface()) return "virtual-interface variable";
                if (t.isDynamicallySizedArray()) return "dynamic array variable";
                if (t.isQueue()) return "queue variable";
                if (t.isAssociativeArray()) return "associative array variable";
                if (auto* element = t.getArrayElementType()) return classifyType(*element);
                const Scope* scope = nullptr;
                switch (t.kind) {
                    case SymbolKind::PackedStructType: scope = &t.as<PackedStructType>(); break;
                    case SymbolKind::UnpackedStructType: scope = &t.as<UnpackedStructType>(); break;
                    case SymbolKind::PackedUnionType: scope = &t.as<PackedUnionType>(); break;
                    case SymbolKind::UnpackedUnionType: scope = &t.as<UnpackedUnionType>(); break;
                    default: break;
                }
                if (scope) for (const auto& field : scope->membersOfType<FieldSymbol>())
                    if (auto* result = classifyType(field.getType())) return result;
                return nullptr;
            };
            const char* what = classifyType(t);
            if (what) emit(Family::DataType, v.location, std::string(what) + " '" + std::string(v.name) + "'");
            this->visitDefault(v);
        }
    };
    void check(Lint& l) override {
        bool fpga = l.context().config.profile == "fpga";
        V v(l, fpga, family);
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (p.procedureKind != ProceduralBlockKind::Final) return;
            FinalScopes scopes;
            p.getBody().visit(scopes);
            v.finalScopes.insert(scopes.scopes.begin(), scopes.scopes.end());
        });
        l.visitHardware(v);
        v.reportForeign();
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-061: system subroutine calls. Synthesizable calls are the allow-list; known simulation-only calls are WARNING;
// anything else is WARNING with Unresolved confidence. Final blocks and assertions are INFO.
const std::set<std::string>& synthesizableTasks() {
    static const std::set<std::string> s = {"$clog2",  "$bits",     "$signed",    "$unsigned",  "$size",     "$high",
                                            "$low",    "$left",     "$right",     "$increment", "$dimensions", "$countones",
                                            "$onehot", "$onehot0",  "$isunknown", "$typename",  "$cast"};
    return s;
}

const std::set<std::string>& knownSimulationTasks() {
    static const std::set<std::string> s = {
        "$display",  "$displayb",   "$displayh",    "$displayo",  "$write",      "$writeb",     "$writeh",
        "$writeo",   "$strobe",     "$strobeb",     "$strobeh",   "$strobeo",    "$monitor",    "$monitorb",
        "$monitorh", "$monitoro",   "$monitoron",   "$monitoroff", "$fopen",     "$fclose",     "$fdisplay",
        "$fdisplayb", "$fdisplayh", "$fdisplayo",   "$fwrite",    "$fwriteb",    "$fwriteh",    "$fwriteo",
        "$fstrobe",  "$fmonitor",   "$fflush",      "$fgetc",     "$fgets",      "$fscanf",     "$fread",
        "$fseek",    "$ftell",      "$rewind",      "$feof",      "$ferror",     "$sscanf",     "$sformat",
        "$sformatf", "$swrite",     "$readmemb",    "$readmemh",  "$writememb",  "$writememh",  "$dumpfile",
        "$dumpvars", "$dumpon",     "$dumpoff",     "$dumpall",   "$dumpflush",  "$dumplimit",  "$finish",
        "$stop",     "$exit",       "$fatal",       "$error",     "$warning",    "$info",       "$time",
        "$stime",    "$realtime",   "$random",      "$urandom",   "$urandom_range", "$dist_uniform", "$dist_normal",
        "$dist_exponential", "$dist_poisson", "$dist_chi_square", "$dist_t", "$dist_erlang", "$test$plusargs",
        "$value$plusargs", "$system", "$timeformat", "$printtimescale", "$asserton", "$assertoff", "$assertkill",
        "$assertcontrol", "$past", "$rose", "$fell", "$stable", "$changed", "$sampled", "$coverage_control",
        "$coverage_get", "$coverage_get_max", "$coverage_merge", "$coverage_save", "$get_coverage",
        "$set_coverage_db_name", "$load_coverage_db", "$stacktrace"};
    return s;
}

class SystemTaskRule final : public LintRule {
public:
    SystemTaskRule() : LintRule("LNT-061") {}

private:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::set<std::string> allowed;
        bool inFinal = false, inAssert = false, inInitial = false;
        V(Lint& lint, std::set<std::string> extra) : l(lint), allowed(std::move(extra)) {
            allowed.insert(synthesizableTasks().begin(), synthesizableTasks().end());
        }

        void check(std::string_view raw, slang::SourceLocation at) {
            if (!l.isUser(at)) return;
            std::string name(raw);
            if (name.empty() || name[0] != '$') name = "$" + name;
            l.count();
            if (allowed.count(name)) return;
            Severity base = Severity::Warning;
            Confidence conf = Confidence::Established;
            std::string what = name + ": system call is not synthesizable";
            if (!knownSimulationTasks().count(name)) {
                conf = Confidence::Unresolved;
                what = name + ": unrecognised system call, not classified as synthesizable";
            }
            if (inAssert || inFinal) base = Severity::Info;
            // An initial block runs once at time zero and is not hardware: a message there is ignored by synthesis, and a memory file
            // read there is the load idiom of FPGA flows (ASIC flows load the macro another way). A value taken from the simulator
            // ($random, $time) or a file read anywhere else still changes what is built.
            static const std::set<std::string> initialOnly = {"$display", "$displayb", "$displayh", "$displayo", "$write", "$writeb",
                                                              "$writeh", "$writeo", "$strobe", "$monitor", "$readmemb", "$readmemh"};
            if (inInitial && initialOnly.count(name)) base = Severity::Info;
            l.report(at, what, {.severity = base, .confidence = conf,
                                .nextStep = "Remove it from synthesizable RTL, or list it in LNT-061.allow if it is a synthesizable profile exception."});
        }
        void handle(const CallExpression& c) {
            if (c.isSystemCall()) check(c.getSubroutineName(), c.sourceRange.start());
            this->visitDefault(c);
        }
        void handle(const ElabSystemTaskSymbol& e) {
            const char* name = nullptr;
            switch (e.taskKind) {
                case ElabSystemTaskKind::Fatal: name = "$fatal"; break;
                case ElabSystemTaskKind::Error: name = "$error"; break;
                case ElabSystemTaskKind::Warning: name = "$warning"; break;
                case ElabSystemTaskKind::Info: name = "$info"; break;
                default: break;
            }
            if (name && l.isUser(e.location))
                l.report(e.location, std::string(name) + ": elaboration-time message, evaluated during elaboration and not synthesized",
                         {.severity = Severity::Info, .confidence = Confidence::Established});
        }
        void handle(const ProceduralBlockSymbol& p) {
            Scoped<bool> f(inFinal, inFinal || p.procedureKind == ProceduralBlockKind::Final);
            Scoped<bool> i(inInitial, p.procedureKind == ProceduralBlockKind::Initial);
            this->visitDefault(p);
        }
        void handle(const ImmediateAssertionStatement& s) {
            Scoped<bool> a(inAssert, true);
            this->visitDefault(s);
        }
        void handle(const ConcurrentAssertionStatement& s) {
            Scoped<bool> a(inAssert, true);
            this->visitDefault(s);
        }
    };
    void check(Lint& l) override {
        std::set<std::string> extra;
        std::string list = l.option("allow", "");
        size_t pos = 0;
        while (pos <= list.size()) {
            size_t end = list.find(',', pos);
            std::string tok = list.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) extra.insert(tok[0] == '$' ? tok : "$" + tok);
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        visitDesign<V>(l, std::move(extra));
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The procedures of every instance by source position. The canonical visit sees one body of identical instances; the model walks all
// of them, so this is where a rule finds the other copies of a block it judged. Only procedures with inventoried writes appear. A
// copy is named by the scope it sits in (instance path, generate block included), which tells two copies of one block apart.
using ProcPosition = std::pair<uint32_t, size_t>;
using ProcCopies = std::map<ProcPosition, std::vector<const ProceduralBlockSymbol*>>;

ProcPosition positionOf(const ProceduralBlockSymbol& p) { return {p.location.buffer().getId(), p.location.offset()}; }

std::string copyName(const ProceduralBlockSymbol& p) {
    const Scope* scope = p.getParentScope();
    return scope ? scope->asSymbol().getHierarchicalPath() : std::string();
}

/// The model nodes of each symbol in `wanted`, found in one pass (a node belongs to one instance of the module the symbol is in).
using NodesOf = std::unordered_map<const ValueSymbol*, std::vector<model::NodeId>>;
void collectNodes(const model::Model& m, NodesOf& nodesOf) {
    if (nodesOf.empty()) return;
    for (model::NodeId n = 0; n < m.nodes.size(); ++n)
        if (m.nodes[n].sym)
            if (auto it = nodesOf.find(m.nodes[n].sym); it != nodesOf.end()) it->second.push_back(n);
}

ProcCopies procedureCopies(Lint& l) {
    ProcCopies copies;
    const auto& m = model::getModel(l.context().design);
    const ProceduralBlockSymbol* last = nullptr;
    for (const auto& write : m.proceduralWrites) {
        if (write.process == last || !write.process || !l.isUser(write.process->location)) continue;
        last = write.process;
        auto& group = copies[positionOf(*write.process)];
        if (std::find(group.begin(), group.end(), write.process) == group.end()) group.push_back(write.process);
    }
    return copies;
}

/// "a, b, c and 2 more" of the names in `names` that are not in `except`.
std::string nameList(const std::set<std::string>& names, const std::set<std::string>& except = {}) {
    std::string s;
    size_t n = 0;
    for (const auto& name : names) {
        if (except.count(name)) continue;
        if (++n <= 4) s += (s.empty() ? "" : ", ") + name;
    }
    if (n > 4) s += " and " + std::to_string(n - 4) + " more";
    return s;
}

/// A write under an `if` whose condition is an elaboration constant that selects the other arm never runs (`else if (P) s <= d;` with P = 0).
/// LNT-018 reports the dead branch; for a coverage question the write is not there.
bool deadWrite(const model::ProceduralWrite& write, Lint& l) {
    for (auto it = write.scopes.begin(); it != write.scopes.end(); ++it) {
        if ((*it)->kind != StatementKind::Conditional || it + 1 == write.scopes.end()) continue;
        const auto& c = (*it)->as<ConditionalStatement>();
        const bool inThen = *(it + 1) == &c.ifTrue;
        if (c.conditions.size() != 1 || c.conditions[0].pattern || (!inThen && *(it + 1) != c.ifFalse) || callsSubroutine(*c.conditions[0].expr)) continue;
        const auto cv = l.evalConst(*c.conditions[0].expr);
        if (cv.isInteger() && !cv.integer().hasUnknown() && static_cast<bool>(cv.isTrue()) != inThen) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-020 reset-only registers (assigned only in the reset branch), LNT-067 registers with no reset value (assigned only
// outside the reset branch), LNT-068 always_ff blocks with no reset in a module whose other always_ff blocks reset.
class ResetCodingRule final : public LintRule {
public:
    enum Kind { ResetOnly, NoResetValue, NoResetBlock };
    explicit ResetCodingRule(Kind k) : LintRule(k == ResetOnly ? "LNT-020" : k == NoResetValue ? "LNT-067" : "LNT-068"), kind_(k) {}

private:
    Kind kind_;
    std::set<const Symbol*> reported_;   // registers LNT-067 already reported from the reset and other branches
    // Bit-precise complement of the statement-level check, from the model's per-bit reset values: bits that no asynchronous reset branch
    // assigns although other bits of the register get a value, and registers of blocks with several asynchronous controls.
    void modelNoResetValue(Lint& l) {
        auto& m = model::getModel(l.context().design);
        struct Sym { std::set<uint32_t> missing, having; const model::Flop* first = nullptr; bool multi = false; };
        std::map<std::string, Sym> syms;
        for (auto& f : m.flops) {
            if (f.resets.empty() || !m.nodes[f.q].sym || !l.isUser(m.nodes[f.q].sym->location)) continue;
            if (reported_.count(m.nodes[f.q].sym)) continue;   // one finding per register: the statement-level check named it
            std::string name = m.nameOf(f.q);
            if (auto b = name.rfind('['); b != std::string::npos && name.back() == ']') name.resize(b);
            auto& s = syms[name];
            if (!s.first) s.first = &f;
            s.multi = s.multi || f.resets.size() >= 2;
            bool has = false;
            for (char ch : f.resetValues) has = has || ch != 'n';
            (has ? s.having : s.missing).insert(m.nodes[f.q].bit);
        }
        for (auto& [name, s] : syms) {
            if (s.missing.empty() || !(s.multi || !s.having.empty())) continue;   // a register with no reset value at all is the statement-level check
            std::string bits;
            for (auto it = s.missing.begin(); it != s.missing.end();) {
                auto lo = *it, hi = lo;
                for (++it; it != s.missing.end() && *it == hi + 1; ++it) hi = *it;
                bits += (bits.empty() ? "" : ", ") + (lo == hi ? "[" + std::to_string(lo) + "]" : "[" + std::to_string(hi) + ":" + std::to_string(lo) + "]");
            }
            l.reportAt(m.procs[s.first->proc].loc, "register '" + name + "' bits " + bits + " have no reset value: no asynchronous reset branch assigns them",
                       {.severity = Severity::Info, .nextStep = "Assign the bits in the reset branch if required by the initialization contract.", .hierarchy = m.procs[s.first->proc].instancePath, .signals = {name}});
        }
    }
    struct BodyInfo {
        bool anyReset = false;
        std::vector<const ProceduralBlockSymbol*> noReset;
        std::vector<const ProceduralBlockSymbol*> undecided;   // no known reset, but a test of undecided identity
    };
    // A finding before it is reported. A module whose instances disagree about a reset test is judged for each instance, and the
    // findings are merged afterwards.
    struct Out {
        const ProceduralBlockSymbol* proc = nullptr;   // reported at this procedure, else at `loc`
        sverity::SourceLocation loc;
        std::string text;
        LintOpts opts;
        const Symbol* once = nullptr;                  // LNT-067 names a register once
        ProcPosition group{0, 0};                      // the block it was judged for, by source position
        std::string where;                             // the copy of that block it was judged for
        const ProceduralBlockSymbol* origin = nullptr; // that copy
    };
    using Sink = std::vector<Out>;

    static std::string keyOf(const Out& o) {
        return "g" + std::to_string(o.group.first) + ":" + std::to_string(o.group.second) + "|" +
               (o.proc ? "p" + std::to_string(o.proc->location.buffer().getId()) + ":" + std::to_string(o.proc->location.offset())
                       : "l" + o.loc.file + ":" + std::to_string(o.loc.line) + ":" + std::to_string(o.loc.column)) + "|" + o.text;
    }

    // The branches that run while a reset is asserted, judged against the rest: LNT-020 (written only there) and LNT-067 (something
    // written elsewhere has no reset value there). `tests` are the procedure's reset tests, or the asynchronous reset's own branch.
    Sink sidesOf(Lint& l, const ProceduralBlockSymbol& p, const std::vector<const model::ResetBranch*>& tests, const Branches& b) {
        Sink found;
        const auto& shared = model::getModel(l.context().design);
        // Per target: what the writes of each branch may reach (`may`: the longest static prefix, a superset) and what
        // they certainly reach (`must`: the elaborated or loop-unrolled range). `unbounded` marks a loop-indexed write
        // whose range the loop analysis cannot prove.
        struct Side {
            const model::ProceduralWrite* first = nullptr;
            WriteRanges may, must;
            std::vector<std::pair<const model::ProceduralWrite*, WriteRanges>> writes;   // each write's interval set
            bool unbounded = false;
            bool uncertain = false;   // a reset write in a loop that may not run every iteration (a branch, an exit)
            bool inDisjunction = false;   // the first write sits in the branch of a disjunctive reset test (`rst || load`)
        };
        std::map<const Symbol*, std::pair<Side, Side>> sides;   // first: reset branch, second: the rest
        auto isLoop = [](const Statement* st) {
            return st->kind == StatementKind::ForLoop || st->kind == StatementKind::ForeachLoop ||
                   st->kind == StatementKind::WhileLoop || st->kind == StatementKind::DoWhileLoop ||
                   st->kind == StatementKind::RepeatLoop;
        };
        for (const auto* wp : shared.writesOf(&p)) {
            const auto& write = *wp;
            if (write.called || !write.target || deadWrite(write, l)) continue;
            const Statement* branch = nullptr;   // the reset branch this write sits in
            const model::ResetBranch* test = nullptr;
            for (const auto* r : tests)
                if (!branch && std::find(write.scopes.begin(), write.scopes.end(), r->reset) != write.scopes.end()) { branch = r->reset; test = r; }
            // The branch of a disjunction (`rst || load`) also runs without the reset. A write of a constant there is a reset write; one
            // that stores a constant only while the net is asserted (`rst ? 1'b0 : din`) is a reset write and a load; any other is a load.
            bool resetWrite = branch != nullptr, loadWrite = branch == nullptr;
            const model::ResetBranch* compound = test && test->compound ? test : nullptr;
            if (compound) {
                // a write on an arm that does not run while the net is asserted (`else q <= din;` after `if (rst) q <= 0;`) is a load only
                const bool skipped = model::reachWhileAsserted(write, *test) == model::Reach::Skipped;
                resetWrite = !skipped && (write.constantValue || model::storesConstantWhileAsserted(write, *test));
                loadWrite = skipped || !write.constantValue;
            }
            auto exact = writeRanges(write, l);
            if (resetWrite) {
                auto& side = sides[write.target].first;
                if (!side.first) side.first = &write;
                side.may.emplace_back(write.lo, write.hi);
                // what the reset branch certainly leaves: `certainRanges` is the one test of a write that must run
                auto certain = certainRanges(write, l, branch, compound);
                if (certain) side.must.insert(side.must.end(), certain->begin(), certain->end());
                else if (exact == std::nullopt && std::any_of(write.scopes.begin(), write.scopes.end(), isLoop)) side.unbounded = true;
                else if (std::any_of(std::find(write.scopes.begin(), write.scopes.end(), branch), write.scopes.end(), isLoop))
                    side.uncertain = true;
                side.writes.emplace_back(&write, exact ? *exact : WriteRanges{{write.lo, write.hi}});
            }
            if (loadWrite) {
                auto& side = sides[write.target].second;
                if (!side.first) { side.first = &write; side.inDisjunction = test && test->compound; }
                side.may.emplace_back(write.lo, write.hi);
                if (exact) side.must.insert(side.must.end(), exact->begin(), exact->end());
                else if (std::any_of(write.scopes.begin(), write.scopes.end(), isLoop)) side.unbounded = true;
                side.writes.emplace_back(&write, exact ? *exact : WriteRanges{{write.lo, write.hi}});
            }
        }
        auto conf = b.exact ? Confidence::Established : Confidence::Heuristic;
        auto add = [&](const model::ProceduralWrite& w, std::string text, LintOpts o, const Symbol* once = nullptr) {
            Out out;
            if (w.fromCall) out.proc = &p; else out.loc = w.loc;
            out.text = std::move(text);
            out.opts = std::move(o);
            out.once = once;
            found.push_back(std::move(out));
        };
        for (auto& [s, pair] : sides) {
            auto& [reset, other] = pair;
            const std::string name(s->name);
            if (kind_ == ResetOnly && reset.first) {
                // A reset write whose elements no other write can reach never loads. A loop whose range is not
                // proven could reset anything the other branch does not load: undecided.
                bool uncovered = false;
                for (auto& [w, ranges] : reset.writes) uncovered = uncovered || !rangesCover(other.may, ranges);
                if (uncovered && reset.unbounded)
                    add(*reset.first, "Unresolved: reset range of '" + name + "' cannot be bounded to compare it with the other branch",
                        {.confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath(), .signals = {name}});
                else if (uncovered)
                    add(*reset.first, "register '" + name + "' is assigned only in the reset branch (reset-only, never loads)",
                        {.confidence = conf, .nextStep = "Assign the register in the non-reset branch too.",
                         .hierarchy = p.getHierarchicalPath(), .signals = {name}});
            }
            if (kind_ == NoResetValue && other.first) {
                // A write outside the reset branch needs a reset write that certainly covers it. A reset loop whose range
                // is not proven, or whose writes may skip an iteration, could cover it: undecided.
                bool uncovered = false, maybeCovered = true;
                WriteRanges missing;
                for (auto& [w, ranges] : other.writes)
                    if (!rangesCover(reset.must, ranges)) {
                        uncovered = true;
                        maybeCovered = maybeCovered && rangesCover(reset.may, ranges);
                        auto left = subtractRanges(ranges, reset.must);
                        missing.insert(missing.end(), left.begin(), left.end());
                    }
                if (uncovered && (reset.unbounded || reset.uncertain) && maybeCovered) {
                    add(*other.first, "Unresolved: reset range of '" + name + "' cannot be bounded to compare it with the other branch",
                        {.confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath(), .signals = {name}}, s);
                } else if (uncovered) {
                    // a register the reset branch assigns in part names the bits it leaves out
                    std::string text = other.inDisjunction
                        ? "register '" + name + "' has no reset value (the branch of its disjunctive reset test also runs without the reset, and its write there is not a constant while the reset is asserted)"
                        : "register '" + name + "' has no reset value (assigned only outside the reset branch)";
                    if (reset.first && s->as_if<ValueSymbol>() && s->as<ValueSymbol>().getType().isIntegral() && !missing.empty()) {
                        std::sort(missing.begin(), missing.end());
                        std::string bits;
                        for (size_t i = 0; i < missing.size();) {
                            auto [lo, hi] = missing[i];
                            for (++i; i < missing.size() && missing[i].first <= hi + 1; ++i) hi = std::max(hi, missing[i].second);
                            bits += (bits.empty() ? "" : ", ") + (lo == hi ? "[" + std::to_string(lo) + "]" : "[" + std::to_string(hi) + ":" + std::to_string(lo) + "]");
                        }
                        text = "register '" + name + "' bits " + bits + " have no reset value: the reset branch does not certainly assign them";
                    }
                    add(*other.first, text,
                        {.severity = Severity::Info, .confidence = conf, .nextStep = "Add a reset value if required by the initialization contract.",
                         .hierarchy = p.getHierarchicalPath(), .signals = {name}}, s);
                }
            }
        }
        return found;
    }

    // The finding when the test is of a net that may or may not be a reset: nothing definite can be said.
    static void undecidedOut(Out& o, const std::string& net) {
        if (o.opts.confidence == Confidence::Unresolved) return;
        const std::string reg = o.opts.signals.empty() ? std::string() : o.opts.signals[0];
        o.text = "Unresolved: whether '" + reg + "' is reset-only or lacks a reset value cannot be decided, since " + net +
                 (net.find(',') == std::string::npos ? " may or may not be a reset" : " may or may not be resets") + " (reset identity undecided)";
        o.opts = {.confidence = Confidence::Unresolved, .hierarchy = o.opts.hierarchy, .signals = o.opts.signals};
    }

    // Judges one procedure. Findings go to `out`; LNT-068 notes the block in `bodies`.
    void judge(Lint& l, const ProceduralBlockSymbol& p, Sink& out, std::map<const InstanceBodySymbol*, BodyInfo>& bodies) {
        auto v = seqView(l, p);
        if (!v) return;
        const auto& shared = model::getModel(l.context().design);
        auto sync = v->edges.size() == 1 ? model::synchronousReset(p, shared) : std::nullopt;
        auto emit = [&](Out o) {
            o.where = copyName(p);
            o.origin = &p;
            o.group = positionOf(p);
            out.push_back(std::move(o));
        };
        auto unresolved = [&](Out o, std::string text) {
            o.text = std::move(text);
            o.opts = {.confidence = Confidence::Unresolved};
            emit(std::move(o));
        };
        bool called = false;
        for (const auto* wp : shared.writesOf(&p)) {
            const auto& write = *wp;
            if (!write.called) continue;
            called = true;
            if (kind_ != NoResetBlock) { Out o; o.loc = write.loc; unresolved(std::move(o), "Unresolved: " + write.reason); }
        }
        if (v->ambiguous && v->chain.empty() && kind_ != NoResetBlock) {
            Out o;
            o.proc = &p;
            unresolved(std::move(o), "Unresolved: reset branches of a procedure with ambiguous edge controls are not classified");
        }
        if (!called && !v->chain.empty()) {
            // Set and reset (several asynchronous controls tested as a priority chain): a write in any control's branch is a reset
            // write, the final else is the rest.
            std::vector<model::ResetBranch> controls;
            for (const auto* branch : v->chain) {
                model::ResetBranch c;
                c.reset = branch;
                controls.push_back(c);
            }
            std::vector<const model::ResetBranch*> tests;
            for (const auto& c : controls) tests.push_back(&c);
            Branches b{v->chain.front(), v->chainOther, true};
            for (auto& o : sidesOf(l, p, tests, b)) emit(std::move(o));
        }
        else if (!called && ((v->reset && !v->ambiguous) || sync)) {
            // The branches that run while a reset is asserted: every KNOWN test of a procedure without asynchronous reset, or the
            // asynchronous reset's own. A test of undecided identity may be a reset too: a finding that holds with and without it is
            // definite, any other is Unresolved.
            std::vector<const model::ResetBranch*> known;
            std::string undecided;
            Branches b;
            if (sync) {
                for (const auto& t : sync->known) known.push_back(&t);
                for (const auto& t : sync->undecided) {
                    const std::string name = "'" + std::string(t.signal->name) + "'";
                    if (undecided.find(name) == std::string::npos) undecided += (undecided.empty() ? "" : ", ") + name;
                }
                const auto& first = sync->known.empty() ? sync->undecided[0] : sync->known[0];
                b = Branches{first.reset, first.other, true};
            } else {
                b = branchesOf(*v);
            }
            if (!b.exact && kind_ != NoResetBlock) {
                Out o;
                o.proc = &p;
                unresolved(std::move(o), "Unresolved: reset-branch attribution is not established");
            }
            if (b.reset) {
                Sink result;
                if (!sync) {
                    model::ResetBranch async;
                    async.reset = b.reset;
                    result = sidesOf(l, p, {&async}, b);
                } else if (undecided.empty()) {
                    result = sidesOf(l, p, known, b);
                } else {
                    // Each undecided test (each polarity of one) read as a reset on its own, and all of them together: a register written
                    // only under two undecided tests is reset-only when both are resets, whatever either does alone. A finding that holds
                    // in every reading and without them is definite. Findings are monotone in the reset tests (more resets: more
                    // reset-only registers, fewer registers without a reset value), so without, each alone and all together bound every
                    // subset.
                    Sink sure = known.empty() ? Sink{} : sidesOf(l, p, known, b);
                    std::vector<Sink> readings;
                    for (const auto& t : sync->undecided) {
                        auto with = known;
                        with.push_back(&t);
                        readings.push_back(sidesOf(l, p, with, b));
                    }
                    // Choose one polarity per signal, consistently across all its tests.
                    using Choice = std::vector<const model::ResetBranch*>;
                    std::map<const ValueSymbol*, std::map<bool, Choice>> bySignal;
                    for (const auto& t : sync->undecided) bySignal[t.signal][t.assertedHigh].push_back(&t);
                    std::vector<std::vector<Choice>> groups;
                    for (const auto& [signal, polarities] : bySignal) {
                        auto& choices = groups.emplace_back();
                        for (const auto& [high, tests] : polarities) choices.push_back(tests);
                    }
                    if (groups.size() >= 2) {
                        size_t combinations = 1;
                        for (const auto& g : groups) {
                            if (combinations > 64 / g.size()) { combinations = 65; break; }
                            combinations *= g.size();
                        }
                        if (combinations > 64) {
                            // An empty reading prevents definite claims; it cannot
                            // inventory candidates hidden in the skipped combinations.
                            readings.emplace_back();
                            if (kind_ != NoResetBlock) {
                                Out obligation;
                                obligation.proc = &p;
                                obligation.text = "Unresolved: reset polarity combinations exceed the 64-reading limit; reset-only and reset-value classification is incomplete for these write targets";
                                obligation.opts.confidence = Confidence::Unresolved;
                                std::set<std::string> targets;
                                for (const auto* write : shared.writesOf(&p))
                                    if (write->target) targets.insert(std::string(write->target->name));
                                obligation.opts.signals.assign(targets.begin(), targets.end());
                                result.push_back(std::move(obligation));
                                l.context().degradedReason = "reset polarity combination limit exceeded";
                            }
                        } else {
                            for (size_t c = 0; c < combinations; ++c) {
                                auto with = known;
                                size_t rest = c;
                                for (const auto& g : groups) {
                                    const auto& choice = g[rest % g.size()];
                                    with.insert(with.end(), choice.begin(), choice.end());
                                    rest /= g.size();
                                }
                                readings.push_back(sidesOf(l, p, with, b));
                            }
                        }
                    } else if (!groups.empty()) {
                        for (const auto& choice : groups.front()) {
                            auto with = known;
                            with.insert(with.end(), choice.begin(), choice.end());
                            readings.push_back(sidesOf(l, p, with, b));
                        }
                    }
                    std::set<std::string> inSure, inEvery, seen;
                    for (const auto& o : sure) inSure.insert(keyOf(o));
                    for (const auto& o : readings.front()) {
                        bool all = true;
                        for (const auto& r : readings)
                            all = all && std::any_of(r.begin(), r.end(), [&](const Out& x) { return keyOf(x) == keyOf(o); });
                        if (all) inEvery.insert(keyOf(o));
                    }
                    for (auto& o : sure) {
                        if (!inEvery.count(keyOf(o))) undecidedOut(o, undecided);
                        seen.insert(keyOf(o));
                        result.push_back(std::move(o));
                    }
                    for (auto& r : readings)
                        for (auto& o : r) {
                            if (inSure.count(keyOf(o)) || !seen.insert(keyOf(o)).second) continue;
                            undecidedOut(o, undecided);
                            result.push_back(std::move(o));
                        }
                }
                for (auto& o : result) emit(std::move(o));
            }
        }
        if (kind_ != NoResetBlock || p.procedureKind != ProceduralBlockKind::AlwaysFF) return;
        // A KNOWN reset in the first condition is a reset; an UNDECIDED net there may be one, so the block is not called unreset.
        bool undecidedTest = false;
        bool hasReset = v->reset != nullptr || (v->first && [&] {
            for (auto& c : v->first->conditions)
                if (findExpr(*c.expr, [&](const Expression& x) {
                        if (x.kind != ExpressionKind::NamedValue) return false;
                        const auto cls = model::classifyReset(shared, x.as<NamedValueExpression>().symbol);
                        undecidedTest = undecidedTest || cls.undecided;
                        return cls.reset;
                    })) return true;
            return false;
        }());
        // An undecided test that guards an arm which only loads data is an enable, not a reset question.
        if (undecidedTest && !hasReset && v->first) {
            std::vector<const ConditionalStatement*> enables;
            model::synchronousReset(p, shared, &enables);
            if (std::find(enables.begin(), enables.end(), v->first) != enables.end()) undecidedTest = false;
        }
        auto& info = bodies[enclosingBody(p)];
        if (hasReset) info.anyReset = true;
        else if (!shared.writesOf(&p).empty()) (undecidedTest ? info.undecided : info.noReset).push_back(&p);
    }

    // What LNT-068 finds in a whole module: blocks without a reset where another block resets.
    void bodyFindings(const std::map<const InstanceBodySymbol*, BodyInfo>& bodies, Sink& out) {
        for (auto& [body, info] : bodies) {
            if (!info.anyReset) continue;
            auto add = [&](const ProceduralBlockSymbol& p, std::string text, LintOpts o) {
                Out f;
                f.proc = &p;
                f.text = std::move(text);
                f.opts = std::move(o);
                f.where = copyName(p);
                f.origin = &p;
                f.group = positionOf(p);
                out.push_back(std::move(f));
            };
            for (auto* p : info.noReset)
                add(*p, "always_ff block has no reset while other always_ff blocks of this module reset their registers",
                    {.severity = Severity::Info, .confidence = Confidence::Heuristic, .nextStep = "Confirm the registers do not need a reset."});
            for (auto* p : info.undecided)
                add(*p, "Unresolved: whether this always_ff block has a reset cannot be decided: its first test is of a net whose reset identity is undecided",
                    {.confidence = Confidence::Unresolved});
        }
    }

    // The reset tests of a procedure as one string: equal for two instances when they test the same nets as resets, with the same
    // polarity, as undecided or as data.
    std::string signatureOf(Lint& l, const ProceduralBlockSymbol& p) {
        auto v = seqView(l, p);
        if (!v) return "-";
        const auto& shared = model::getModel(l.context().design);
        std::string s;
        if (v->edges.size() == 1)
            if (auto sync = model::synchronousReset(p, shared)) {
                for (const auto& t : sync->known) s += "K" + std::string(t.reset == &t.test->ifTrue ? "t" : "e") + std::to_string(t.test->sourceRange.start().offset()) + ";";
                for (const auto& t : sync->undecided) s += "U" + std::to_string(t.test->sourceRange.start().offset()) + ";";
            }
        if (kind_ == NoResetBlock && v->first)
            for (auto& c : v->first->conditions)
                findExpr(*c.expr, [&](const Expression& x) {
                    if (x.kind != ExpressionKind::NamedValue) return false;
                    const auto cls = model::classifyReset(shared, x.as<NamedValueExpression>().symbol);
                    s += cls.reset ? "k" : cls.undecided ? "u" : "d";
                    return false;
                });
        return s;
    }

    void emit(Lint& l, const Out& o) {
        if (o.once && !reported_.insert(o.once).second) return;
        if (o.proc) l.report(*o.proc, o.text, o.opts);
        else l.reportAt(o.loc, o.text, o.opts);
    }

    void check(Lint& l) override {
        reported_.clear();
        std::vector<const ProceduralBlockSymbol*> visited;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && seqView(l, p)) visited.push_back(&p);
        });
        // The procedures of every instance by source position: the model walks all instances, the visit above only one body of each.
        const ProcCopies bySource = procedureCopies(l);
        // A module whose instances disagree about a reset test (a reset in one, data in another) is judged for each of them.
        std::map<const ProceduralBlockSymbol*, std::string> signatures;
        auto signature = [&](const ProceduralBlockSymbol* q) -> const std::string& {
            auto it = signatures.find(q);
            return it != signatures.end() ? it->second : signatures.emplace(q, signatureOf(l, *q)).first->second;
        };
        std::set<const void*> disagreeing;
        for (const auto* p : visited) {
            auto it = bySource.find(positionOf(*p));
            if (it == bySource.end() || it->second.size() < 2) continue;
            for (const auto* q : it->second)
                if (signature(q) != signature(p)) {
                    if (const auto* body = enclosingBody(*p)) disagreeing.insert(&body->getDefinition());
                    break;
                }
        }
        Sink direct, split;
        std::map<const InstanceBodySymbol*, BodyInfo> bodies, splitBodies;
        for (const auto* p : visited) {
            const auto* body = enclosingBody(*p);
            if (!body || !disagreeing.count(&body->getDefinition())) judge(l, *p, direct, bodies);
        }
        std::set<const ProceduralBlockSymbol*> done;
        for (auto& [pos, group] : bySource)
            for (const auto* q : group) {
                const auto* body = enclosingBody(*q);
                if (!body || !disagreeing.count(&body->getDefinition()) || !done.insert(q).second) continue;
                judge(l, *q, split, splitBodies);
            }
        bodyFindings(bodies, direct);
        bodyFindings(splitBodies, split);
        for (const auto& o : direct) emit(l, o);
        // One finding per source line for the instances that disagree: reported as it is where every copy of the block that was judged
        // gives it, Unresolved where only some do, naming them. A copy is one occurrence of the block's syntax (an instance, a generate
        // block of it): an instance whose parameters leave the block out is not a copy and does not count against the finding.
        std::map<std::string, std::vector<const Out*>> byKey;
        std::vector<std::string> order;
        for (const auto& o : split) {
            if (!byKey.count(keyOf(o))) order.push_back(keyOf(o));
            byKey[keyOf(o)].push_back(&o);
        }
        for (const auto& key : order) {
            const auto& group = byKey[key];
            std::set<std::string> with, without;
            std::set<const ProceduralBlockSymbol*> givers;
            for (const auto* o : group) { with.insert(o->where); givers.insert(o->origin); if (o->once) reported_.insert(o->once); }
            Out merged = *group[0];
            merged.once = nullptr;
            size_t copies = 0;
            if (auto it = bySource.find(merged.group); it != bySource.end()) {
                copies = it->second.size();
                for (const auto* q : it->second)
                    if (!givers.count(q)) without.insert(copyName(*q));
            }
            if (givers.size() < copies && merged.opts.confidence != Confidence::Unresolved) {
                merged.text = "Unresolved: " + merged.text + "; the instances of this module disagree on a reset test and the finding does not hold for every copy of this block (reported for " +
                              nameList(with) + ", not for " + nameList(without) + ")";
                merged.opts = {.confidence = Confidence::Unresolved, .hierarchy = merged.opts.hierarchy, .signals = merged.opts.signals};
            }
            emit(l, merged);
        }
        if (kind_ == NoResetValue) modelNoResetValue(l);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-022: one signal controls a register on both posedge and negedge.
class BothEdgesRule final : public LintRule {
public:
    BothEdgesRule() : LintRule("LNT-022") {}

private:
    using ControlBit = std::pair<const ValueSymbol*, uint64_t>;
    static std::optional<ControlBit> controlBit(const SignalEventControl& event, const ProceduralBlockSymbol& proc) {
        slang::ast::EvalContext ec(proc);
        ValuePath path(event.expr, ec);
        const auto* root = path.rootSymbol();
        const auto* value = root ? root->as_if<ValueSymbol>() : nullptr;
        if (!value || !path.isFullyStatic()) return std::nullopt;
        if (path.lsp) {
            if (path.lspBounds.first != path.lspBounds.second) return std::nullopt;
            return ControlBit{value, path.lspBounds.first};
        }
        if (value->getType().isIntegral() && value->getType().getBitWidth() == 1) return ControlBit{value, 0};
        return std::nullopt;
    }
    struct ControlRoot {
        model::NodeId node;
        bool inverted = false;
        bool operator==(const ControlRoot&) const = default;
    };
    static std::optional<ControlRoot> checkedRoot(const model::Model& m, model::NodeId node) {
        bool inverted = false;
        std::set<model::NodeId> seen;
        for (size_t depth = 0; depth < 256; ++depth) {
            if (!seen.insert(node).second || (node < m.unresolvedDriven.size() && m.unresolvedDriven[node])) return std::nullopt;
            const auto edges = m.in[node];
            if (edges.empty()) return ControlRoot{node, inverted};
            if (edges.size() != 1) {
                for (auto e : edges) if (m.edges[e].type == model::EdgeType::Comb) return std::nullopt;
                return ControlRoot{node, inverted};
            }
            const auto& edge = m.edges[edges[0]];
            if (edge.type != model::EdgeType::Comb) return ControlRoot{node, inverted};
            if (!(edge.exact || edge.flip)) return std::nullopt;
            inverted = inverted != edge.flip;
            node = edge.from;
        }
        return std::nullopt;
    }
    // Two different one-bit signals on opposite edges of one block that are the same net in some instance (`wire c2 = clk;` used as
    // `negedge c2` beside `posedge clk`; two ports of a child tied to one net): the model's alias root of both is one node. Judged for each
    // copy of the block, one finding per event, naming the copies that have it. Separate-procedure aliases are handled below through the same per-instance graph.
    template <class Pairs>
    void aliasedEvents(Lint& l, const Pairs& pairs) {
        if (pairs.empty()) return;
        const ProcCopies copies = procedureCopies(l);
        struct Copy { const ProceduralBlockSymbol* proc; const SignalEventControl* first; const SignalEventControl* second; size_t i, j; };
        std::vector<Copy> list;
        std::set<std::tuple<const ProceduralBlockSymbol*, size_t, size_t>> taken;
        for (const auto& pr : pairs) {
            auto add = [&](const ProceduralBlockSymbol& q) {
                if (!taken.insert({&q, pr.i, pr.j}).second) return;
                if (auto v = seqView(l, q); v && pr.j < v->edges.size()) list.push_back({&q, v->edges[pr.i], v->edges[pr.j], pr.i, pr.j});
            };
            add(*pr.proc);
            if (auto it = copies.find(positionOf(*pr.proc)); it != copies.end())
                for (const auto* q : it->second) add(*q);
        }
        NodesOf nodesOf;
        for (const auto& c : list) {
            if (auto bit = controlBit(*c.first, *c.proc)) nodesOf[bit->first];
            if (auto bit = controlBit(*c.second, *c.proc)) nodesOf[bit->first];
        }
        auto& m = model::getModel(l.context().design);
        collectNodes(m, nodesOf);
        struct Found { slang::SourceLocation at; std::string a, b; const ProceduralBlockSymbol* first; std::set<std::string> with; };
        std::map<ProcPosition, Found> found;
        std::map<ProcPosition, std::set<std::string>> copiesAt;
        for (const auto& c : list) {
            const auto abit = controlBit(*c.first, *c.proc), bbit = controlBit(*c.second, *c.proc);
            if (!abit || !bbit) continue;
            const auto* a = abit->first;
            const auto* b = bbit->first;
            const auto at = c.second->expr.sourceRange.start();
            const ProcPosition key{at.buffer().getId(), at.offset()};
            copiesAt[key].insert(copyName(*c.proc));
            auto nodeIn = [&](const ControlBit& bit) -> model::NodeId {
                model::NodeId hit = model::kNoNode;
                for (model::NodeId n : nodesOf[bit.first])
                    if (m.nodes[n].bit == bit.second && m.nodes[n].inst && &m.nodes[n].inst->body == enclosingBody(*c.proc)) {
                        if (hit != model::kNoNode) return model::kNoNode;   // not one node: no claim
                        hit = n;
                    }
                return hit;
            };
            const auto na = nodeIn(*abit), nb = nodeIn(*bbit);
            const auto ar = na == model::kNoNode ? std::nullopt : checkedRoot(m, na);
            const auto br = nb == model::kNoNode ? std::nullopt : checkedRoot(m, nb);
            if (!ar || !br) {
                l.context().degradedReason = "opposite-edge control aliases are unresolved";
                l.report(at, "Unresolved: opposite-edge control aliases cannot be established",
                         {.confidence = Confidence::Unresolved, .hierarchy = c.proc->getHierarchicalPath()});
                continue;
            }
            if (*ar != *br) continue;
            auto [it, fresh] = found.emplace(key, Found{at, std::string(a->name), std::string(b->name), c.proc, {}});
            it->second.with.insert(copyName(*c.proc));
        }
        for (const auto& [key, f] : found) {
            const auto& all = copiesAt[key];
            const std::string where = f.with.size() < all.size() ? " (found for " + nameList(f.with) + ", not for " + nameList(all, f.with) + ")" : std::string();
            l.report(f.at, "'" + f.a + "' and '" + f.b + "' are the same net and control the same register on both posedge and negedge" + where,
                     {.nextStep = "Use one clock edge per register.", .hierarchy = f.first->getHierarchicalPath(), .signals = {f.a, f.b}});
        }
    }
    void check(Lint& l) override {
        struct Writer { const ProceduralBlockSymbol* proc; const SignalEventControl* event; Ctl control; const ValueSymbol* target; uint64_t lo, hi; };
        std::vector<Writer> writers;
        struct Pair { const ProceduralBlockSymbol* proc; size_t i, j; };   // opposite edges on two different signals: one net behind both?
        std::vector<Pair> pairs;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v) return;
            for (const auto* wp : model::getModel(l.context().design).writesOf(&p)) {
                const auto& write = *wp;
                if (!write.target || write.called) continue;
                for (size_t i = 0; i < v->edges.size(); ++i)
                    writers.push_back({&p, v->edges[i], v->ctl[i], write.target, write.lo, write.hi});
            }
            const auto& e = v->edges;
            for (size_t i = 0; i < e.size(); i++)
                for (size_t j = i + 1; j < e.size(); j++) {
                    const Symbol* a = symOf(e[i]->expr);
                    const Symbol* b = symOf(e[j]->expr);
                    if (a && b && !v->ctl[i].empty() && v->ctl[i] == v->ctl[j] && e[i]->edge != e[j]->edge)
                        l.report(e[j]->expr.sourceRange.start(),
                                 "'" + std::string(a->name) + "' controls the same register on both posedge and negedge",
                                 {.nextStep = "Use one clock edge per register.", .hierarchy = p.getHierarchicalPath(),
                                  .signals = {std::string(a->name)}});
                    else if (a && b && e[i]->edge != e[j]->edge && controlBit(*e[i], p) && controlBit(*e[j], p))
                        pairs.push_back({&p, i, j});
                    else if (e[i]->edge != e[j]->edge) {
                        l.context().degradedReason = "opposite-edge control selection is unresolved";
                        l.report(e[j]->expr.sourceRange.start(), "Unresolved: opposite-edge control expressions may select the same signal bit",
                                 {.confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath()});
                    }
                }
        });
        aliasedEvents(l, pairs);
        std::set<std::tuple<std::string, uint32_t, uint32_t>> calledSites;
        for (const auto& write : model::getModel(l.context().design).proceduralWrites)
            if (write.called && calledSites.insert({write.loc.file, write.loc.line, write.loc.column}).second && seqView(l, *write.process) && l.isUser(write.process->location))
                l.reportAt(write.loc, "Unresolved: " + write.reason,
                           {.confidence = Confidence::Unresolved});
        // Include noncanonical procedure copies: port connections can make two
        // controls aliases in only a later instance.
        auto& model = model::getModel(l.context().design);
        std::set<const ProceduralBlockSymbol*> inventoried;
        for (const auto& w : writers) inventoried.insert(w.proc);
        std::set<const ProceduralBlockSymbol*> pending;
        for (const auto& w : model.proceduralWrites)
            if (w.target && !w.called && !inventoried.count(w.process)) pending.insert(w.process);
        for (const auto* proc : pending) {
            if (!l.isUser(proc->location)) continue;
            auto v = seqView(l, *proc);
            if (!v) continue;
            for (const auto* w : model.writesOf(proc)) {
                if (!w->target || w->called) continue;
                for (size_t i = 0; i < v->edges.size(); ++i)
                    writers.push_back({proc, v->edges[i], v->ctl[i], w->target, w->lo, w->hi});
            }
        }
        NodesOf nodes;
        for (const auto& w : writers)
            if (auto bit = controlBit(*w.event, *w.proc)) nodes[bit->first];
        collectNodes(model, nodes);
        auto aliasRoot = [&](const Writer& w) -> std::optional<ControlRoot> {
            const auto bit = controlBit(*w.event, *w.proc);
            if (!bit) return std::nullopt;
            model::NodeId node = model::kNoNode;
            for (auto candidate : nodes[bit->first])
                if (model.nodes[candidate].bit == bit->second && model.nodes[candidate].inst &&
                    &model.nodes[candidate].inst->body == enclosingBody(*w.proc)) {
                    if (node != model::kNoNode) return std::nullopt;
                    node = candidate;
                }
            return node == model::kNoNode ? std::nullopt : checkedRoot(model, node);
        };
        struct Finding {
            const Writer* writer = nullptr;
            bool unresolved = false;
            std::set<std::string> copies;
        };
        std::map<std::pair<ProcPosition, ProcPosition>, Finding> findings;
        std::unordered_map<const ValueSymbol*, std::vector<size_t>> byTarget;
        for (size_t i = 0; i < writers.size(); ++i) byTarget[writers[i].target].push_back(i);
        for (const auto& [target, indices] : byTarget)
          for (size_t x = 0; x < indices.size(); ++x) for (size_t y = x + 1; y < indices.size(); ++y) {
            const auto& a = writers[indices[x]]; const auto& b = writers[indices[y]];
            if (a.proc == b.proc || a.event->edge == b.event->edge || a.hi < b.lo || b.hi < a.lo) continue;
            bool matches = !a.control.empty() && a.control == b.control;
            bool unresolved = false;
            if (!matches) {
                const auto ar = aliasRoot(a), br = aliasRoot(b);
                if (ar && br) matches = *ar == *br;
                else unresolved = true;
            }
            if (!matches && !unresolved) continue;
            const auto al = a.event->expr.sourceRange.start(), bl = b.event->expr.sourceRange.start();
            auto key = std::make_pair(ProcPosition{al.buffer().getId(), al.offset()}, ProcPosition{bl.buffer().getId(), bl.offset()});
            if (key.second < key.first) std::swap(key.first, key.second);
            auto& f = findings[key];
            if (!f.writer) f.writer = &b;
            f.unresolved = f.unresolved || unresolved;
            f.copies.insert(copyName(*b.proc));
        }
        for (const auto& [key, f] : findings) {
            const auto& w = *f.writer;
            if (f.unresolved) l.context().degradedReason = "opposite-edge control aliases are unresolved";
            l.report(w.event->expr.sourceRange.start(),
                     (f.unresolved ? "Unresolved: separate opposite-edge writers may share a control bit" :
                                     "one net controls the same register on both posedge and negedge in separate procedures") +
                         std::string(" (found for ") + nameList(f.copies) + ")",
                     {.confidence = f.unresolved ? Confidence::Unresolved : Confidence::Established,
                      .hierarchy = w.proc->getHierarchicalPath(), .signals = {std::string(w.target->name)}});
        }

    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-069: an edge control of a sequential block that the first if / else-if chain does not test. Every edge control but the
// clock must be an asynchronous control tested by that chain; otherwise the block has several clock-like controls or an
// untested asynchronous control. One finding per block.
class UntestedEdgeControlRule final : public LintRule {
public:
    UntestedEdgeControlRule() : LintRule("LNT-069") {}

private:
    void check(Lint& l) override {
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v || v->edges.size() < 2) return;
            const auto& e = v->edges;
            // Distinct controls: key by the control path, or by the event itself when it is not a signal reference.
            using Key = std::pair<Ctl, const void*>;
            std::vector<Key> keys;
            std::vector<bool> recognised;
            std::vector<std::string> names;
            for (size_t i = 0; i < e.size(); i++) {
                Key key{v->ctl[i], v->ctl[i].empty() ? static_cast<const void*>(e[i]) : nullptr};
                auto it = std::find(keys.begin(), keys.end(), key);
                if (it == keys.end()) {
                    keys.push_back(key);
                    recognised.push_back(v->tested[i]);
                    names.push_back(ctlName(v->ctl[i]));
                }
                else {
                    recognised[static_cast<size_t>(it - keys.begin())] = recognised[static_cast<size_t>(it - keys.begin())] || v->tested[i];
                }
            }
            if (keys.size() >= 2) {
                size_t nRec = static_cast<size_t>(std::count(recognised.begin(), recognised.end(), true));
                if (nRec == keys.size()) {   // the chain tests every control: none is left to be the clock
                    l.report(p, "every edge control is tested; no clock remains",
                             {.severity = Severity::Warning, .confidence = Confidence::Established,
                              .nextStep = "Leave the clock out of the first if / else-if chain; test only the asynchronous controls there.",
                              .hierarchy = p.getHierarchicalPath()});
                    return;
                }
                if (nRec + 1 < keys.size()) {
                    std::string clocks;
                    for (size_t i = 0; i < keys.size(); i++)
                        if (!recognised[i]) clocks += (clocks.empty() ? "" : ", ") + names[i];
                    // No edge control is used anywhere in the block: there is no asynchronous control to map, and the
                    // block also updates on the second edge. A user call could read a control, so it stays undecided.
                    bool anyUse = false;
                    // The base of a select is not a use of its own: `r[1]` reads r[1], not all of r.
                    std::set<const Expression*> bases;
                    findExprIn(*v->body, [&](const Expression& x) {
                        if (x.kind == ExpressionKind::ElementSelect) bases.insert(&x.as<ElementSelectExpression>().value());
                        else if (x.kind == ExpressionKind::RangeSelect) bases.insert(&x.as<RangeSelectExpression>().value());
                        else if (x.kind == ExpressionKind::MemberAccess) bases.insert(&x.as<MemberAccessExpression>().value());
                        return false;
                    });
                    for (size_t i = 0; i < e.size(); i++)
                        if (v->ctl[i].empty() || findExprIn(*v->body, [&](const Expression& x) {
                                return (isDirectRef(x) && !bases.count(&x) && mayRead(ctlOf(l, x), v->ctl[i])) ||
                                       (x.kind == ExpressionKind::Call && !x.as<CallExpression>().isSystemCall());
                            }))
                            anyUse = true;
                    if (!anyUse) {
                        l.report(p, "event list has " + std::to_string(keys.size()) + " edge controls (" + clocks +
                                        ") but the block uses none of them: it cannot be built as a flip-flop with an asynchronous control",
                                 {.severity = Severity::Error, .confidence = Confidence::Established,
                                  .nextStep = "Use one clock per register; add the other controls as resets tested by the first if.",
                                  .hierarchy = p.getHierarchicalPath()});
                        return;
                    }
                    l.report(p, "event list has " + std::to_string(keys.size()) + " edge controls but only " + std::to_string(nRec) +
                                    " are recognised as async resets; clock-like: " + clocks,
                             {.confidence = Confidence::Heuristic, .nextStep = "Use one clock per register; add the other controls as resets tested by the first if.",
                              .hierarchy = p.getHierarchicalPath()});
                    return;
                }
            }
            if (v->ambiguous && e.size() >= 3) {   // several asynchronous controls (set + reset, power-on + functional)
                size_t nTested = static_cast<size_t>(std::count(v->tested.begin(), v->tested.end(), true));
                if (nTested + 1 != e.size()) {
                    l.report(p, "block has " + std::to_string(e.size()) + " edge events but only " + std::to_string(nTested) +
                                " are tested by the if / else-if chain: the asynchronous controls are not all coded as a priority chain",
                             {.confidence = Confidence::Heuristic, .nextStep = "Test every asynchronous control in the first if / else-if chain.", .hierarchy = p.getHierarchicalPath()});
                }
                return;
            }
            if (!v->ambiguous && v->reset && v->heuristicReset) {
                std::string rname = ctlName(v->resetCtl);
                l.report(p, "async reset '" + rname + "' is not tested by the first if",
                         {.confidence = Confidence::Heuristic, .nextStep = "Test the reset as the first if condition, with the matching polarity.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {rname}});
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-084: one net used as an asynchronous reset (in an event list) and as a synchronous reset (tested in a clocked block
// without it in its event list) in the same module.
class MixedResetStyleRule final : public LintRule {
public:
    MixedResetStyleRule() : LintRule("LNT-084") {}

private:
    void check(Lint& l) override {
        const auto& shared = model::getModel(l.context().design);
        struct Use { const ProceduralBlockSymbol* proc; const ValueSymbol* signal; };
        struct Body { std::map<const ValueSymbol*, const ProceduralBlockSymbol*> async; std::vector<Use> sync; };
        std::map<const InstanceBodySymbol*, Body> bodies;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v) return;
            auto& body = bodies[enclosingBody(p)];
            auto asyncOf = [&](const Ctl& c) {
                const Ctl root = ctlRoot(c);
                if (root.size() == 1 && root[0].sym) if (const auto* sym = root[0].sym->as_if<ValueSymbol>()) body.async.emplace(sym, &p);
            };
            if (v->reset && !v->ambiguous) asyncOf(v->resetCtl);
            for (const auto& c : v->chainCtl) asyncOf(c);
            if (v->edges.size() == 1)
                if (auto sync = model::synchronousReset(p, shared))
                    for (const auto& t : sync->known)
                        if (t.signal) body.sync.push_back({&p, t.signal});
        });
        for (auto& [bodySym, body] : bodies) {
            std::set<std::pair<const ProceduralBlockSymbol*, const ValueSymbol*>> done;
            for (const auto& use : body.sync) {
                auto it = body.async.find(use.signal);
                if (it == body.async.end() || !done.insert({use.proc, use.signal}).second) continue;
                l.count();
                const std::string name(use.signal->name);
                const auto at = l.loc(it->second->location);
                l.report(*use.proc, "reset '" + name + "' is a synchronous reset in this block and an asynchronous reset in the block at line " +
                                        std::to_string(at.line) + " of the same module",
                         {.severity = Severity::Warning, .confidence = Confidence::Established,
                          .nextStep = "Use one reset style per reset net: add it to this block's event list, or give the synchronous registers their own reset.",
                          .hierarchy = use.proc->getHierarchicalPath(), .signals = {name}});
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-085: registers of one module clocked on both edges of one clock net (different registers; one register on both edges is
// LNT-022).
class MixedClockEdgeRule final : public LintRule {
public:
    MixedClockEdgeRule() : LintRule("LNT-085") {}

private:
    void check(Lint& l) override {
        const auto& shared = model::getModel(l.context().design);
        struct Clocked { const ProceduralBlockSymbol* proc; std::set<const Symbol*> targets; };
        struct Clock { std::vector<Clocked> pos, neg; };
        std::map<std::pair<const InstanceBodySymbol*, std::string>, Clock> clocks;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v) return;
            const SignalEventControl* clock = v->clock;
            size_t index = clock ? static_cast<size_t>(std::find(v->edges.begin(), v->edges.end(), clock) - v->edges.begin()) : 0;
            if (!clock && !v->chain.empty()) { index = v->chainClock; clock = v->edges[index]; }
            if (!clock || v->ctl[index].empty() || (clock->edge != EdgeKind::PosEdge && clock->edge != EdgeKind::NegEdge)) return;
            Clocked c{&p, {}};
            for (const auto* w : shared.writesOf(&p))
                if (w->target) c.targets.insert(w->target);
            if (c.targets.empty()) return;
            auto& entry = clocks[{enclosingBody(p), ctlName(ctlRoot(v->ctl[index]))}];
            (clock->edge == EdgeKind::PosEdge ? entry.pos : entry.neg).push_back(std::move(c));
        });
        for (auto& [key, clock] : clocks) {
            for (const auto& n : clock.neg) {
                const Clocked* partner = nullptr;
                for (const auto& q : clock.pos) {
                    const bool shares = std::any_of(n.targets.begin(), n.targets.end(), [&](const Symbol* t) { return q.targets.count(t) != 0; });
                    if (!shares) { partner = &q; break; }
                }
                if (!partner) continue;
                l.count();
                const std::string& name = key.second;
                l.report(*n.proc, "clock '" + name + "' also triggers registers on its rising edge in this module (block at line " +
                                      std::to_string(l.loc(partner->proc->location).line) + "): the design uses both edges of one clock",
                         {.severity = Severity::Info, .confidence = Confidence::Established,
                          .nextStep = "Confirm both edges are intended; timing, duty-cycle and scan constraints must cover each.",
                          .hierarchy = n.proc->getHierarchicalPath(), .signals = {name}});
                break;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-047: edge events (clock or async reset) that are not plain signals (an operator inside the event expression),
// or signals driven by combinational logic (a continuous assign or always_comb whose RHS is not a plain alias).
class DataDrivenEventRule final : public LintRule {
public:
    DataDrivenEventRule() : LintRule("LNT-047") {}

private:
    // What bits of an unconditional copy's right side hold: a plain signal (or a select of one) the bits come from, or a constant.
    // `inverted` marks a bit that passed one inversion; `lo..hi` are bits of that operand.
    struct Alias { const Expression* source = nullptr; bool constant = false; bool inverted = false; uint64_t lo = 0, hi = 0; };
    /// The operand that survives a `?:` whose select is an elaboration constant (`Stub ? '0 : rst_ni` is `rst_ni` when Stub = 0).
    static const Expression& chosenOperand(Lint& l, const Expression& e0) {
        const Expression* e = &strip(e0);
        while (e->kind == ExpressionKind::ConditionalOp) {
            const auto& c = e->as<ConditionalExpression>();
            if (c.conditions.size() != 1 || c.conditions[0].pattern) break;
            auto cv = l.evalConst(*c.conditions[0].expr);
            if (!cv.isInteger() || cv.integer().hasUnknown()) break;
            e = &strip(cv.isFalse() ? c.right() : c.left());
        }
        return *e;
    }
    /// The sources of bits `lo..hi` of a right side that only moves bits: a plain signal, a constant, a concatenation of such
    /// operands (the bits go to the operand that holds them), an inversion of one (`~x`, or `!x` of one bit; two cancel).
    /// False when any of these bits comes through logic (a gate, a mux on a signal).
    static bool aliasBits(Lint& l, const Expression& rhs, uint64_t lo, uint64_t hi, bool inverted, std::vector<Alias>& out) {
        const Expression& chosen = chosenOperand(l, rhs);
        if (isPlain(chosen)) { out.push_back({&chosen, false, inverted, lo, hi}); return true; }
        auto cv = l.evalConst(chosen);
        if (cv.isInteger() && !cv.integer().hasUnknown()) { out.push_back({nullptr, true, inverted, lo, hi}); return true; }
        if (chosen.kind == ExpressionKind::Concatenation) {
            uint64_t base = 0;
            const auto& operands = chosen.as<ConcatenationExpression>().operands();
            for (auto it = operands.rbegin(); it != operands.rend(); ++it) {   // the last operand holds the least significant bits
                const uint64_t width = (*it)->type ? (*it)->type->getBitstreamWidth() : 0;
                if (!width) return false;
                if (base <= hi && lo < base + width && !aliasBits(l, **it, std::max(lo, base) - base, std::min(hi, base + width - 1) - base, inverted, out))
                    return false;
                base += width;
            }
            return base > hi;
        }
        if (chosen.kind == ExpressionKind::Replication) {
            // `{2{x}}`: bit k of the result is bit k % width of the operand
            const auto& r = chosen.as<ReplicationExpression>();
            const uint64_t width = r.concat().type ? r.concat().type->getBitstreamWidth() : 0;
            if (!width) return false;
            for (uint64_t k = lo; k <= hi; ++k)
                if (!aliasBits(l, r.concat(), k % width, k % width, inverted, out)) return false;
            return true;
        }
        if (chosen.kind == ExpressionKind::UnaryOp && chosen.type && chosen.type->isIntegral()) {
            const auto& u = chosen.as<UnaryExpression>();
            const uint64_t width = chosen.type->getBitstreamWidth();
            const bool flips = (u.op == UnaryOperator::BitwiseNot && u.operand().type->getBitstreamWidth() == width) ||
                               (u.op == UnaryOperator::LogicalNot && width == 1 && u.operand().type->getBitstreamWidth() == 1);
            if (flips) return aliasBits(l, u.operand(), lo, hi, !inverted, out);
        }
        return false;
    }
    // Follow only unconditional copies. Two guarded copies implement a mux, not an alias.
    static bool aliasSource(Lint& l, const ValueDriver& d, const ValueSymbol& sym, uint64_t lo, uint64_t hi, std::vector<Alias>& out) {
        const Symbol& cs = *d.containingSymbol;
        const Expression* rhs = nullptr;
        if (cs.kind == SymbolKind::ContinuousAssign) {
            const Expression& a = cs.as<ContinuousAssignSymbol>().getAssignment();
            if (a.kind != ExpressionKind::Assignment) return false;
            rhs = &a.as<AssignmentExpression>().right();
        } else if (cs.kind == SymbolKind::ProceduralBlock) {
            const Statement* st = &cs.as<ProceduralBlockSymbol>().getBody();
            for (;;) {
                if (st->kind == StatementKind::Timed) st = &st->as<TimedStatement>().stmt;
                else if (st->kind == StatementKind::Block) st = &st->as<BlockStatement>().body;
                else if (st->kind == StatementKind::List && st->as<StatementList>().list.size() == 1)
                    st = st->as<StatementList>().list[0];
                else break;
            }
            if (st->kind != StatementKind::ExpressionStatement) return false;
            auto as = assignsIn(st);
            if (as.size() != 1 || as[0]->left().getSymbolReference() != &sym) return false;
            rhs = &as[0]->right();
        } else {
            return false;
        }
        const uint64_t width = rhs->type ? rhs->type->getBitstreamWidth() : 0;
        if (!width) return false;
        return aliasBits(l, *rhs, lo, std::min(hi, width - 1), false, out);
    }

    using Bits = std::pair<uint64_t, uint64_t>;

    // One edge event of one copy of a block: the signal it names, and whether one inversion sits in the event itself.
    struct Event {
        const ProceduralBlockSymbol* proc = nullptr;
        const SignalEventControl* control = nullptr;
        const Expression* signal = nullptr;   // the event's expression without a single inversion
        const ValueSymbol* sym = nullptr;
        bool eventInverted = false;
        bool plain = false;                   // the event is a signal, not an expression
        bool isClock = false;                 // the block's clock event (not an asynchronous control)
    };
    static Event eventOf(const ProceduralBlockSymbol& p, const SignalEventControl& e) {
        Event ev;
        ev.proc = &p;
        ev.control = &e;
        const Expression* exp = &strip(e.expr);
        // A single inversion of a plain signal (`posedge ~clk`) is glitch-free and built as written: INFO, and the signal is followed on.
        if (exp->kind == ExpressionKind::UnaryOp && exp->type && exp->type->isIntegral()) {
            const auto& u = exp->as<UnaryExpression>();
            const Expression& inner = strip(u.operand());
            if ((u.op == UnaryOperator::BitwiseNot || u.op == UnaryOperator::LogicalNot) && isPlain(inner) &&
                inner.type && inner.type->getBitstreamWidth() == exp->type->getBitstreamWidth() && exp->type->getBitstreamWidth() == 1) {
                exp = &inner;
                ev.eventInverted = true;
            }
        }
        ev.signal = exp;
        ev.plain = isPlain(*exp);
        if (ev.plain)
            if (auto* sym = symOf(*exp)) ev.sym = sym->as_if<ValueSymbol>();
        return ev;
    }

    // What drives the signal when the hazard is a combinational driver: where it is, which net it drives, and what kind of driver it is.
    // A latch-based clock gate (a latch transparent while the clock is low, ANDed with the clock) and a clock mux whose select a register
    // or a constant sets are glitch-free by construction: advisory. A mux on a free input, or any other logic, is the hazard.
    enum class DriverKind { Logic, ClockGate, ClockMux };
    struct DriverInfo {
        bool known = false;
        SourceLocation loc;
        std::string hierarchy, net;
        DriverKind kind = DriverKind::Logic;
        std::string why;   // for an advisory kind
    };
    // `registerClock`: a clock event whose signal a clocked process drives (a divided or generated clock); `reg` names that register.
    struct Verdict { bool hazard = false, unknown = false, inverter = false; DriverInfo driver; bool registerClock = false; std::string reg; };

    // What drives the event signal of one copy of a block. The same procedure text in another instance has its own symbols and its own
    // drivers in the model, so every copy is traced on its own (a gated clock into the second `sub` is not a property of the first).
    static Verdict judgeEvent(Lint& l, const Event& ev, const NodesOf& nodesOf) {
        const ProceduralBlockSymbol& p = *ev.proc;
        const Expression& ex = *ev.signal;
        const ValueSymbol* vs = ev.sym;
        auto* an = l.context().design.analysis.get();
        // The event selects bits of its signal; a driver or model path that cannot reach them is not its driver.
        slang::ast::EvalContext eventCtx(p);
        Bits eventBits = prefixBits(ex, *vs, eventCtx).value_or(Bits{0, UINT64_MAX});
        struct Pending { const ValueSymbol* sym; Bits bits; bool inverted; };
        std::set<const ValueSymbol*> seen;
        std::vector<Pending> pending{{vs, eventBits, false}};
        bool hazard = false, unknown = false, inverter = ev.eventInverted, registerClock = false;
        std::string reg;
        DriverInfo driver;
        // The copies of a signal an alias list names are followed on: constants end the chain, anything that is not a signal is unknown.
        auto followAliases = [&](const std::vector<Alias>& aliases, bool inverted) {
            for (const auto& alias : aliases) {
                if (alias.constant) continue;
                auto* sourceSym = symOf(strip(*alias.source));
                auto* value = sourceSym ? sourceSym->as_if<ValueSymbol>() : nullptr;
                if (!value) { unknown = true; continue; }
                // The operand's bits sit at the bits its select takes of the signal, when the widths agree.
                slang::ast::EvalContext sourceCtx(p);
                Bits source{0, UINT64_MAX};
                auto selected = prefixBits(*alias.source, *value, sourceCtx);
                if (selected && selected->second != UINT64_MAX && alias.source->type &&
                    selected->second - selected->first + 1 == alias.source->type->getBitstreamWidth())
                    source = {selected->first + alias.lo, selected->first + alias.hi};
                pending.push_back({value, source, inverted != alias.inverted});
            }
        };
        while (!pending.empty()) {
          auto [current, bits, inverted] = pending.back(); pending.pop_back();
          if (!seen.insert(current).second) { unknown = true; continue; }
          bool continued = false;
          // A net declaration assignment (`wire c = ~x;`) is no driver the analysis lists, but it is the continuous copy `assign c = ~x;`.
          if (const auto* net = current->as_if<NetSymbol>(); net && net->getInitializer() && an->getDrivers(*current).empty()) {
              const Expression& init = *net->getInitializer();
              const uint64_t width = init.type ? init.type->getBitstreamWidth() : 0;
              std::vector<Alias> aliases;
              if (width && bits.first < width && aliasBits(l, init, bits.first, std::min(bits.second, width - 1), false, aliases)) {
                  continued = true;
                  followAliases(aliases, inverted);
              }
          }
          for (const ValueDriver* d : an->getDrivers(*current)) {
            if (d->isUnidirectionalPort()) continue;
            Bits driven = d->path.lsp ? d->getBounds() : Bits{0, UINT64_MAX};
            if (driven.first > driven.second) std::swap(driven.first, driven.second);
            if (driven.second < bits.first || bits.second < driven.first) continue;
            const Symbol& cs = *d->containingSymbol;
            bool comb = false;
            if (cs.kind == SymbolKind::ContinuousAssign) comb = true;
            else if (cs.kind == SymbolKind::ProceduralBlock) {
                auto& pb = cs.as<ProceduralBlockSymbol>();
                comb = pb.procedureKind == ProceduralBlockKind::AlwaysComb ||
                       pb.procedureKind == ProceduralBlockKind::AlwaysLatch ||
                       (pb.procedureKind == ProceduralBlockKind::Always && !seqView(l, pb));
            }
            if (!comb) {
                if (ev.isClock && !registerClock && cs.kind == SymbolKind::ProceduralBlock && seqView(l, cs.as<ProceduralBlockSymbol>())) {
                    registerClock = true;
                    reg = std::string(current->name);
                }
                continue;
            }
            // The event bits sit at `bits - driven.first` in the copy's right side.
            std::vector<Alias> aliases;
            if (aliasSource(l, *d, *current, std::max(bits.first, driven.first) - driven.first,
                            std::min(bits.second, driven.second) - driven.first, aliases)) {
                continued = true;
                followAliases(aliases, inverted);
                continue;
            }
            hazard = true;
            if (!driver.known) {
                driver.known = true;
                driver.loc = l.loc(d->getSourceRange().start());
                driver.hierarchy = cs.getHierarchicalPath();
                driver.net = std::string(current->name);
            }
          }
          // An inverted copy of a signal that is not itself a copy is an inverter on it.
          if (inverted && !continued) inverter = true;
        }
        auto& m = model::getModel(l.context().design);
        bool modelDriver = false;   // the driver found in the model replaces the one found in the syntax: it knows the select and the latch
        std::set<std::pair<model::NodeId, bool>> visited;   // node, reached through an odd number of inversions
        std::vector<std::pair<model::NodeId, bool>> queue;
        const bool packedBits = vs->getType().isIntegral();
        if (auto it = nodesOf.find(vs); it != nodesOf.end())
            for (model::NodeId n : it->second)
                if (m.nodes[n].inst && &m.nodes[n].inst->body == enclosingBody(p) &&
                    (!packedBits || m.collapsed(n) || (m.nodes[n].bit >= eventBits.first && m.nodes[n].bit <= eventBits.second)))
                    queue.emplace_back(n, false);
        if (queue.empty()) unknown = true;
        while (!queue.empty()) {
            auto [n, odd] = queue.back(); queue.pop_back();
            if (!visited.insert({n, odd}).second) continue;
            auto root = m.traceRoot(n);
            if (root.kind == model::RootKind::BlackBox || root.kind == model::RootKind::Cycle ||
                (root.kind == model::RootKind::Undriven && !m.flopAt(root.node))) unknown = true;   // a register's output has its flop for driver, not an edge
            bool continued = false;
            for (auto index : m.in[n]) {
                const auto& edge = m.edges[index];
                if (edge.type != model::EdgeType::Comb) continue;
                bool logic = false;
                if (edge.ctrl) logic = true;
                else if (edge.exact) { queue.emplace_back(edge.from, odd); continued = true; }
                else if (edge.flip) { queue.emplace_back(edge.from, !odd); continued = true; }   // `~x`: still a copy, inverted
                else logic = true;
                if (!logic) continue;
                hazard = true;
                if (edge.proc < m.procs.size() && !modelDriver) {
                    modelDriver = true;
                    driver = classifyDriver(m, n, edge.proc);
                }
            }
            if (odd && !continued) inverter = true;   // an odd number of inversions of a signal is an inverter on it
        }
        return {hazard, unknown, inverter, driver, registerClock, reg};
    }

    /// The driver of node `n` that process `proc` makes: its location, and whether it is a clock gate or a clock mux that cannot glitch.
    static DriverInfo classifyDriver(const model::Model& m, model::NodeId n, uint32_t proc) {
        DriverInfo info;
        info.known = true;
        info.loc = m.procs[proc].loc;
        info.hierarchy = m.procs[proc].instancePath;
        info.net = m.nodes[n].sym ? std::string(m.nodes[n].sym->name) : m.nameOf(n);
        std::vector<model::NodeId> data, select;
        bool notAnd = false;   // some source reaches the net through an operator that is neither an AND nor a mux
        for (auto index : m.in[n]) {
            const auto& edge = m.edges[index];
            if (edge.type != model::EdgeType::Comb || edge.proc != proc) continue;
            auto& list = edge.ctrl || edge.op == model::Op::MuxSel ? select : data;
            if (std::find(list.begin(), list.end(), edge.from) == list.end()) list.push_back(edge.from);
            if (!edge.ctrl && edge.op != model::Op::MuxSel && edge.op != model::Op::And && edge.op != model::Op::Mux) notAnd = true;
        }
        // a latch gates the clock: the AND of the clock with the output of a latch that is open only while the clock is low
        if (select.empty() && data.size() == 2 && !notAnd) {
            for (int k = 0; k < 2; ++k) {
                const model::NodeId clock = data[k], enable = data[1 - k];
                // The latch must be transparent only while the clock is low, and the clock its only opener: an `if (clk)` latch is open
                // while the AND passes the clock, and any other term in the condition can open it at the wrong time.
                bool latchOnly = false, openLow = false;
                int openers = 0;
                for (auto index : m.in[enable]) {
                    const auto& edge = m.edges[index];
                    if (edge.type != model::EdgeType::Comb) continue;
                    if (edge.proc >= m.procs.size() || m.procs[edge.proc].kind != model::ProcKind::Latch) { latchOnly = false; break; }
                    latchOnly = true;
                    if (!edge.ctrl) continue;
                    ++openers;
                    if (edge.from == clock &&
                        ((edge.branch == model::Branch::Taken && edge.inv == 1) || (edge.branch == model::Branch::NotTaken && edge.inv == 0)))
                        openLow = true;
                }
                if (openers != 1) openLow = false;
                if (latchOnly && openLow) {
                    info.kind = DriverKind::ClockGate;
                    info.why = "a clock gate: the clock ANDed with the output of a latch that is open while the clock is low, so the enable cannot glitch the output";
                    return info;
                }
            }
        }
        // a mux whose select a register or a constant sets: the clock switches only when that value changes
        if (select.size() == 1 && !data.empty() && !notAnd) {
            const auto root = m.traceRoot(select[0]);
            const char constant = m.constantValue(root.node);
            // a register select changes without glitching only when it runs on one of the clocks the mux chooses between: a register on a
            // third clock changes at any moment relative to both
            bool sameClock = false;
            if (const auto* flop = m.flopAt(root.node); flop && flop->clockResolved && flop->otherClockSignals.empty())
                for (auto arm : data) sameClock = sameClock || m.traceRoot(arm).node == flop->clockRoot.node;
            if (sameClock || constant == '0' || constant == '1') {
                info.kind = DriverKind::ClockMux;
                info.why = sameClock ? "a clock mux whose select is a register on one of the muxed clocks: it changes synchronously, not with the data"
                                     : "a clock mux whose select is a constant";
            }
        }
        return info;
    }

    void check(Lint& l) override {
        // The edge events of every copy of every sequential block: the canonical visit sees one body of identical instances, so the
        // other instances' copies come from the shared inventory of procedures by source position. Each copy is judged on its own and
        // the findings are merged per source line.
        std::vector<Event> events;
        std::set<const ProceduralBlockSymbol*> taken;
        const ProcCopies copies = procedureCopies(l);
        auto take = [&](const ProceduralBlockSymbol& q, bool canonical) {
            if (!taken.insert(&q).second) return;
            auto v = seqView(l, q);
            if (!v) return;
            for (auto* e : v->edges) {
                Event ev = eventOf(q, *e);
                ev.isClock = e == v->clock;
                if (!ev.plain && !canonical) continue;   // the expression finding is syntactic: the canonical copy reports it
                events.push_back(ev);
            }
        };
        std::vector<const ProceduralBlockSymbol*> visited;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && seqView(l, p)) visited.push_back(&p);
        });
        for (const auto* p : visited) take(*p, true);
        std::set<ProcPosition> expanded;
        for (const auto* p : visited)
            if (expanded.insert(positionOf(*p)).second)
                if (auto it = copies.find(positionOf(*p)); it != copies.end())
                    for (const auto* q : it->second) take(*q, false);
        // The nodes of every event signal, found in one pass over the model.
        NodesOf nodesOf;
        for (const auto& ev : events)
            if (ev.sym) nodesOf[ev.sym];
        collectNodes(model::getModel(l.context().design), nodesOf);
        // A combinational driver is one finding, at the driver, whatever number of register blocks it clocks or resets.
        struct Consumer { slang::SourceLocation at; std::string signal, hierarchy; };
        struct Driven { DriverInfo driver; std::vector<Consumer> consumers; };
        std::map<std::string, Driven> driven;
        struct Merged { slang::SourceLocation at; std::string signal; int kind; std::set<std::string> with; std::vector<const ProceduralBlockSymbol*> procs; std::string reg; };
        std::map<std::pair<ProcPosition, int>, size_t> index;   // (event position, kind) -> entry
        std::vector<Merged> merged;
        std::map<ProcPosition, std::set<std::string>> copiesAt;   // every copy judged for an event, by the event's position
        auto positionOfEvent = [](const Event& ev) {
            const auto at = ev.control->expr.sourceRange.start();
            return ProcPosition{at.buffer().getId(), at.offset()};
        };
        for (const auto& ev : events) {
            const auto at = ev.control->expr.sourceRange.start();
            if (!ev.plain) {
                l.report(at, "edge event is an expression, not a signal",
                         {.nextStep = "Drive the clock or reset from a port or register.", .hierarchy = ev.proc->getHierarchicalPath()});
                continue;
            }
            if (!ev.sym) continue;
            if (!l.context().design.analysis) {
                l.report(at, "analysis unavailable: cannot verify that the edge signal is not combinational",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = ev.proc->getHierarchicalPath()});
                continue;
            }
            const Verdict v = judgeEvent(l, ev, nodesOf);
            const std::string copy = copyName(*ev.proc);
            copiesAt[positionOfEvent(ev)].insert(copy);
            auto note = [&](int kind) {
                auto [it, fresh] = index.emplace(std::pair{positionOfEvent(ev), kind}, merged.size());
                if (fresh) merged.push_back({at, std::string(ev.sym->name), kind, {}, {}, v.reg});
                merged[it->second].with.insert(copy);
                merged[it->second].procs.push_back(ev.proc);
            };
            if (v.hazard && v.driver.known) {
                const auto& d = v.driver;
                auto& group = driven[d.loc.file + ":" + std::to_string(d.loc.line) + ":" + std::to_string(d.loc.column) + ":" + d.net + ":" + d.hierarchy];
                group.driver = d;
                group.consumers.push_back({at, std::string(ev.sym->name), ev.proc->getHierarchicalPath()});
            }
            else if (v.hazard) note(0);
            else {
                if (v.inverter) note(1);
                if (v.unknown) note(2);
                if (v.registerClock) note(3);
            }
        }
        for (const auto& [key, group] : driven) {
            const bool advisory = group.driver.kind != DriverKind::Logic;
            const size_t n = group.consumers.size();
            std::vector<Evidence> evidence;
            for (size_t i = 0; i < n && i < 8; ++i)
                evidence.push_back({"edge_event", "edge event of '" + group.consumers[i].signal + "' in " + group.consumers[i].hierarchy, l.loc(group.consumers[i].at)});
            if (n > 8) evidence.push_back({"edge_event", "and " + std::to_string(n - 8) + " more register blocks", {}});
            l.reportAt(group.driver.loc,
                       // the same words for every copy of the driver, so the copies are the occurrences of one finding
                       "'" + group.driver.net + "' is driven by combinational logic and is used as an edge event (clock or reset)" +
                           (advisory ? ": " + group.driver.why : std::string()),
                       {.severity = advisory ? Severity::Info : Severity::Warning, .confidence = Confidence::Heuristic,
                        .nextStep = advisory ? "No action unless the cell is not what it looks like." : "Clock or reset from a register or a port, not from logic.",
                        .hierarchy = group.driver.hierarchy, .signals = {group.driver.net}, .evidence = std::move(evidence)});
        }
        for (const auto& f : merged) {
            const auto& all = copiesAt[{f.at.buffer().getId(), f.at.offset()}];
            // The copies judged for this event: said when the others do not give the finding, so the line is not read as design-wide.
            const std::string where = f.with.size() < all.size() ? " (found for " + nameList(f.with) + ", not for " + nameList(all, f.with) + ")" : std::string();
            // one report per copy that has it: the reporter merges them into one finding that counts the occurrences
            for (const auto* proc : f.procs) {
                const std::string hierarchy = proc->getHierarchicalPath();
                if (f.kind == 0)
                    l.report(f.at, "edge event '" + f.signal + "' is driven by combinational logic" + where,
                             {.confidence = Confidence::Heuristic, .nextStep = "Clock or reset from a register or a port, not from logic.",
                              .hierarchy = hierarchy, .signals = {f.signal}});
                else if (f.kind == 3)
                    l.report(f.at, "clock '" + f.signal + "' is the output of register '" + f.reg + "' (a derived clock)" + where,
                             {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                              .nextStep = "Synthesis builds it as written; give the derived clock its own timing constraint and confirm that a register sampling data from the parent clock domain is intended.",
                              .hierarchy = hierarchy, .signals = {f.signal}});
                else if (f.kind == 1)
                    l.report(f.at, "edge event '" + f.signal + "' is an inversion of a signal" + where,
                             {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                              .nextStep = "Confirm the inverted clock or reset is intended; an inverter adds no glitch.",
                              .hierarchy = hierarchy, .signals = {f.signal}});
                else
                    l.report(f.at, "edge signal alias chain cannot be resolved" + where,
                             {.confidence = Confidence::Unresolved, .hierarchy = hierarchy});
            }
        }
        sampledAcrossDerivedClock(l);
    }

    // A register-derived clock can sample parent-domain data in a different order in hardware and simulation.
    // Report one obligation per derived/source clock pair.
    static void sampledAcrossDerivedClock(Lint& l) {
        auto& m = model::getModel(l.context().design);
        auto& cones = model::sharedCones(m);
        struct Site { SourceLocation at; std::string hierarchy, signal; };
        std::map<std::pair<uint32_t, uint32_t>, Site> sites;   // (register that makes the clock, source domain) -> first sampling register
        std::map<std::pair<uint32_t, uint32_t>, std::pair<std::string, uint32_t>> order;   // for a stable choice: (file:line, column)
        for (const auto& f : m.flops) {
            if (!f.clockResolved || f.proc >= m.procs.size() || !m.nodes[f.q].sym || !l.isUser(m.nodes[f.q].sym->location)) continue;
            // the register behind the clock: followed through exact copies and one-bit inversions only
            const model::Flop* maker = nullptr;
            model::NodeId at = f.clockSignal;
            for (int step = 0; step < 16 && at != model::kNoNode && !maker; ++step) {
                if (const auto* g = m.flopAt(at)) { maker = g; break; }
                model::NodeId next = model::kNoNode;
                int copies = 0;
                for (auto index : m.in[at]) {
                    const auto& edge = m.edges[index];
                    if (edge.type != model::EdgeType::Comb) continue;
                    if (edge.exact || edge.flip) { next = edge.from; ++copies; } else { copies = 2; break; }
                }
                at = copies == 1 ? next : model::kNoNode;
            }
            if (!maker || !maker->clockResolved || maker->q == f.q) continue;
            std::set<uint32_t> parent{maker->domain};
            parent.insert(maker->otherDomains.begin(), maker->otherDomains.end());
            if (parent.count(f.domain)) continue;
            const auto cone = cones.of(f);
            std::set<uint32_t> hit;
            for (const auto& src : cone->flops) {
                const auto* sf = m.flopAt(src.q);
                if (!sf || !sf->clockResolved) continue;
                if (sf->domain != f.domain && parent.count(sf->domain)) hit.insert(sf->domain);
                for (auto other : sf->otherDomains) if (other != f.domain && parent.count(other)) hit.insert(other);
            }
            for (auto domain : hit) {
                const auto& loc = m.procs[f.proc].loc;
                const std::pair<uint32_t, uint32_t> key{maker->q, domain};
                const std::pair<std::string, uint32_t> where{loc.file + ":" + std::to_string(loc.line), loc.column};
                if (auto it = order.find(key); it != order.end() && !(where < it->second)) continue;
                order[key] = where;
                sites[key] = Site{loc, m.procs[f.proc].instancePath, std::string(m.nodes[f.q].sym->name)};
            }
        }
        for (const auto& [key, site] : sites) {
            const auto leaf = [&](model::NodeId n) { return m.nodes[n].sym ? std::string(m.nodes[n].sym->name) : m.nameOf(n); };
            const std::string maker = leaf(key.first), source = leaf(m.domains[key.second].root);
            l.reportAt(site.at,
                     "register '" + site.signal + "' runs on the derived clock of register '" + maker + "' and samples data written on clock '" + source +
                         "', the clock the derived clock is made from: whether it reads the old or the new value depends on the clock-tree skew between them",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Constrain the derived clock against its source (a generated clock with a skew bound), or pass the data through a synchroniser; confirm the intended edge ordering.",
                      .hierarchy = site.hierarchy, .signals = {site.signal}, .obligation = "derived-clock-sampling-order"});
        }
    }
};

/// Calls `fn(assignment, conditional)` for every assignment under `st`; `conditional` is set for one that sits under an if or
/// case whose condition is not a constant, so that a run-time value chooses between assignments. Loops are walked as written.
template <typename F>
void forEachAssign(Lint& l, const Statement* st, bool conditional, F& fn) {
    if (!st) return;
    switch (st->kind) {
        case StatementKind::Block: forEachAssign(l, &st->as<BlockStatement>().body, conditional, fn); return;
        case StatementKind::List:
            for (auto* c : st->as<StatementList>().list) forEachAssign(l, c, conditional, fn);
            return;
        case StatementKind::Conditional: {
            auto& c = st->as<ConditionalStatement>();
            bool dyn = conditional;
            for (auto& cond : c.conditions) dyn = dyn || cond.pattern || !static_cast<bool>(l.evalConst(*cond.expr));
            forEachAssign(l, &c.ifTrue, dyn, fn);
            forEachAssign(l, c.ifFalse, dyn, fn);
            return;
        }
        case StatementKind::Case: {
            auto& c = st->as<CaseStatement>();
            const bool dyn = conditional || !static_cast<bool>(l.evalConst(c.expr));
            for (auto& item : c.items) forEachAssign(l, item.stmt, dyn, fn);
            forEachAssign(l, c.defaultCase, dyn, fn);
            return;
        }
        default:
            for (auto* a : assignsIn(st)) fn(*a, conditional);
            return;
    }
}

/// A statement made of assignments only: no if, case or loop around them.
bool flatAssigns(const Statement* st) {
    if (!st) return true;
    switch (st->kind) {
        case StatementKind::Block: return flatAssigns(&st->as<BlockStatement>().body);
        case StatementKind::List:
            for (auto* c : st->as<StatementList>().list)
                if (!flatAssigns(c)) return false;
            return true;
        case StatementKind::ExpressionStatement:
        case StatementKind::VariableDeclaration:
        case StatementKind::Empty: return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------
// LNT-058 polarity of an asynchronous control must match its edge (also in blocks with several controls); LNT-070 the reset
// branch of an asynchronous reset must assign constants. (A control the first if does not test is LNT-069.)
class AsyncResetControlRule final : public LintRule {
public:
    enum Kind { Polarity, NonConstant };
    explicit AsyncResetControlRule(Kind k) : LintRule(k == Polarity ? "LNT-058" : "LNT-070"), kind_(k) {}

private:
    Kind kind_;
    // The control is tested, but not by a test whose polarity can be read: the mismatch is neither shown nor excluded.
    static void unreadable(Lint& l, const ProceduralBlockSymbol& p, const Ctl& c) {
        const std::string name = ctlName(c);
        l.report(p, "the polarity of the test of asynchronous control '" + name +
                        "' cannot be read: the branch it selects is not known",
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                  .nextStep = "Test the control directly as the first if condition, as 'if (!" + name +
                              ")' for active-low or 'if (" + name + ")' for active-high.",
                  .hierarchy = p.getHierarchicalPath(), .signals = {name}});
    }
    // LNT-070: a reset value must be a constant, and must not be chosen by a run-time condition inside the reset branch.
    void nonConstantReset(Lint& l, const ProceduralBlockSymbol& p, const AssignmentExpression& a, bool conditional, std::set<const Symbol*>& chosen) {
        if (kind_ != NonConstant) return;
        const bool constant = static_cast<bool>(l.evalConst(a.right()));
        if (constant && !conditional) return;
        if (constant && !chosen.insert(a.left().getSymbolReference()).second) return;   // one finding per target chosen by a condition
        const std::string target(a.left().getSymbolReference() ? a.left().getSymbolReference()->name : std::string_view("?"));
        l.report(a.right().sourceRange.start(),
                 constant ? "reset branch assigns '" + target + "' a value chosen by a run-time condition: the reset value is not a constant"
                          : "reset branch assigns a non-constant value to '" + target + "'",
                 {.severity = Severity::Warning, .confidence = !constant && hasUserCall(a.right()) ? Confidence::Unresolved : Confidence::Established,
                  .nextStep = "Reset values must be constants.", .hierarchy = p.getHierarchicalPath()});
    }

    // A signal in the test that is not an edge control of the block (constants and parameters are not signals).
    static const Expression* foreignSignal(Lint& l, const Expression& test, const SeqView& v) {
        std::set<const Symbol*> roots;
        for (auto& c : v.ctl)
            if (!c.empty()) roots.insert(c[0].sym);
        (void)l;
        return findExpr(test, [&](const Expression& x) {
            if (x.kind != ExpressionKind::NamedValue && x.kind != ExpressionKind::HierarchicalValue) return false;
            const Symbol& sy = x.as<ValueExpressionBase>().symbol;
            return (sy.kind == SymbolKind::Variable || sy.kind == SymbolKind::Net || sy.kind == SymbolKind::FormalArgument) && !roots.count(&sy);
        });
    }
    // A user function in the test may read any state: whether the test depends on more than the control is not known.
    static void callInTest(Lint& l, const ProceduralBlockSymbol& p, const Expression& test, const Ctl& c) {
        if (l.evalConst(test).isInteger() || !hasUserCall(test)) return;
        const std::string name = ctlName(c);
        l.report(p, "the reset test of '" + name + "' calls a function: whether it depends on anything but the control is not known",
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                  .nextStep = "Test the control directly in the first if; evaluate other terms in the clocked branch.",
                  .hierarchy = p.getHierarchicalPath(), .signals = {name}, .identityKey = "call-in-test"});
    }
    static void foreignInTest(Lint& l, const ProceduralBlockSymbol& p, const std::string& control, const Expression& hit) {
        const std::string other(hit.as<ValueExpressionBase>().symbol.name);
        l.report(p, "the reset test also depends on '" + other + "', which is not an edge control: no flip-flop has this asynchronous control, and simulation and synthesis differ",
                 {.severity = Severity::Error, .confidence = Confidence::Established,
                  .nextStep = "Test only the asynchronous control in the first if; gate the other term in the clocked branch.",
                  .hierarchy = p.getHierarchicalPath(), .signals = {control, other}});
    }
    static void constantInTest(Lint& l, const ProceduralBlockSymbol& p, const Ctl& c, const std::string& why) {
        const std::string name = ctlName(c);
        l.report(p, "the reset test of '" + name + "' " + why,
                 {.severity = Severity::Error, .confidence = Confidence::Established,
                  .nextStep = "Test the asynchronous control directly; move constant terms out of the test.",
                  .hierarchy = p.getHierarchicalPath(), .signals = {name}, .identityKey = "constant-test"});
    }

    void check(Lint& l) override {
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            const size_t nEdges = v ? v->edges.size() : 0;
            const size_t nTested = v ? static_cast<size_t>(std::count(v->tested.begin(), v->tested.end(), true)) : 0;
            // several asynchronous controls (set + reset, power-on + functional), or every control tested (no clock: LNT-069)
            if (v && v->ambiguous && (nEdges >= 3 || nTested == nEdges)) {
                if (nTested + 1 != nEdges && nTested != nEdges) return;   // untested controls: LNT-069
                if (kind_ == NonConstant) {   // every branch that tests an asynchronous control assigns constants
                    for (const ConditionalStatement* c = v->first; c; c = asIf(c->ifFalse)) {
                        bool testsControl = false;
                        for (size_t i = 0; i < v->edges.size(); i++) {
                            if (!v->tested[i]) continue;
                            for (auto& cond : c->conditions) testsControl = testsControl || findCtl(l, *cond.expr, v->ctl[i]);
                        }
                        if (!testsControl) continue;
                        std::set<const Symbol*> chosen;
                        auto each = [&](const AssignmentExpression& a, bool conditional) { nonConstantReset(l, p, a, conditional, chosen); };
                        forEachAssign(l, &c->ifTrue, false, each);
                    }
                    return;
                }
                std::set<const Expression*> reportedForeign;
                for (size_t i = 0; i < v->edges.size(); i++) {
                    if (!v->tested[i]) continue;
                    const Ctl& sy = v->ctl[i];
                    if (sy.empty()) continue;
                    const std::string sname = ctlName(sy);
                    for (const ConditionalStatement* c = v->first; c; c = asIf(c->ifFalse)) {
                        int pol = -1;
                        bool refs = false, flagged = false;
                        for (auto& cond : c->conditions) {
                            if (!findCtl(l, *cond.expr, sy)) continue;
                            refs = true;
                            pol = testPolarity(l, *cond.expr, sy);
                            // every tested control gets the same checks of its test as the lone reset of a two-edge block
                            if (auto* hit = foreignSignal(l, *cond.expr, *v)) {
                                if (reportedForeign.insert(cond.expr).second) foreignInTest(l, p, sname, *hit);
                                flagged = true;
                            }
                            else if (auto why = constantTestProblem(l, *cond.expr, sy, v->edges[i]->edge); !why.empty()) {
                                constantInTest(l, p, sy, why);
                                flagged = true;
                            }
                            else callInTest(l, p, *cond.expr, sy);
                        }
                        if (!refs) continue;
                        if (flagged) break;
                        if (pol < 0) {
                            if (kind_ == Polarity) unreadable(l, p, sy);
                            break;
                        }
                        bool neg = v->edges[i]->edge == EdgeKind::NegEdge;
                        if (((pol & 1) == 1) != neg)
                            l.report(p, std::string(neg ? "negedge" : "posedge") + " '" + sname + "' but the branch that tests it runs when '" + sname +
                                            (neg ? "' is high" : "' is low") + ": polarity mismatch",
                                     {.severity = Severity::Error, .confidence = Confidence::Established,
                                      .nextStep = neg ? "Test 'if (!" + sname + ")' for an active-low control." : "Test 'if (" + sname + ")' for an active-high control.",
                                      .hierarchy = p.getHierarchicalPath(), .signals = {sname}});
                        break;
                    }
                }
                return;
            }
            if (!v || v->edges.size() < 2 || v->ambiguous || !v->reset) return;
            std::string rname = ctlName(v->resetCtl);
            bool negedge = v->reset->edge == EdgeKind::NegEdge;
            if (v->heuristicReset) return;   // the first if does not test the reset: LNT-069
            // The reset test also reads a signal the event list does not watch (`!rst_n && !x`, `rst_n || x`): the block runs on
            // the reset edge only, so the test sees that signal at the edge, and no flip-flop has such an asynchronous control.
            // One finding for it, whichever arm the test selects.
            if (kind_ == Polarity && v->polarity >= 0) {
                const Expression& test = *v->first->conditions[0].expr;
                if (auto* hit = foreignSignal(l, test, *v)) {
                    foreignInTest(l, p, rname, *hit);
                    return;
                }
                if (auto why = constantTestProblem(l, test, v->resetCtl, v->reset->edge); !why.empty()) {
                    constantInTest(l, p, v->resetCtl, why);
                    return;
                }
                callInTest(l, p, test, v->resetCtl);
            }
            if (kind_ == Polarity && v->polarity >= 0 && !v->activeOnTrue()) {
                // The control's active level selects the else arm. When that arm assigns only constants and the arm the
                // test selects on the inactive level does not, the reset is coded in the else branch (functionally right).
                auto onlyConstants = [&](const Statement* st) {
                    auto as = assignsIn(st);
                    return !as.empty() && std::all_of(as.begin(), as.end(), [&](auto* a) { return static_cast<bool>(l.evalConst(a->right())); });
                };
                auto hasNonConstant = [&](const Statement* st) {
                    auto as = assignsIn(st);
                    return std::any_of(as.begin(), as.end(), [&](auto* a) { return !static_cast<bool>(l.evalConst(a->right())); });
                };
                // Only a bare test of the control puts the reset in the else arm; with any other term (`rst_n || x`) the
                // else arm can be skipped while the control is active, and an else arm that chooses its value is no reset value.
                const bool bareTest = testPolarity(l, *v->first->conditions[0].expr, v->resetCtl, false) >= 0;
                if (bareTest && v->first->ifFalse && flatAssigns(v->first->ifFalse) && onlyConstants(v->first->ifFalse) && hasNonConstant(&v->first->ifTrue)) {
                    l.report(p, "reset coded in the else branch; some synthesis tools require the reset test first",
                             {.severity = Severity::Warning, .confidence = Confidence::Established,
                              .nextStep = negedge ? "Test 'if (!" + rname + ")' first and assign the reset values there." : "Test 'if (" + rname + ")' first and assign the reset values there.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {rname}});
                    return;
                }
                l.report(p, std::string(negedge ? "negedge" : "posedge") + " '" + rname + "' but the reset branch runs when '" + rname +
                                (negedge ? "' is high" : "' is low") + ": polarity mismatch",
                         {.severity = Severity::Error, .confidence = Confidence::Established,
                          .nextStep = negedge ? "Test 'if (!" + rname + ")' for an active-low reset." : "Test 'if (" + rname + ")' for an active-high reset.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {rname}});
            }
            if (kind_ == Polarity && v->polarity < 0 && !v->resetCtl.empty()) unreadable(l, p, v->resetCtl);
            if (v->polarity >= 0 && !v->activeOnTrue()) return;   // mismatch already reported; the reset branch is unclear
            if (kind_ != NonConstant) return;
            Branches b = branchesOf(*v);
            if (!b.reset || !b.exact) return;
            std::set<const Symbol*> chosen;
                        auto each = [&](const AssignmentExpression& a, bool conditional) { nonConstantReset(l, p, a, conditional, chosen); };
            forEachAssign(l, b.reset, false, each);
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// A target writes only nets when every symbol it writes is a net: a concatenation, select or member access counts through
// its operands and root (`{w1, w2}`, `wv[3:2]`, `ws.f`). A variable, a formal or a target of any other kind is not a net.
bool netTarget(const Expression& e0) {
    const Expression& e = strip(e0);
    switch (e.kind) {
        case ExpressionKind::Concatenation: {
            const auto ops = e.as<ConcatenationExpression>().operands();
            return !ops.empty() && std::ranges::all_of(ops, [](const Expression* o) { return o && netTarget(*o); });
        }
        case ExpressionKind::ElementSelect: return netTarget(e.as<ElementSelectExpression>().value());
        case ExpressionKind::RangeSelect: return netTarget(e.as<RangeSelectExpression>().value());
        case ExpressionKind::MemberAccess:
            // A struct field is judged by the object it is in; a member reached through an interface port is the net itself.
            if (e.as<MemberAccessExpression>().member.kind == SymbolKind::Field) return netTarget(e.as<MemberAccessExpression>().value());
            break;
        default: break;
    }
    const Symbol* s = e.getSymbolReference();
    return s && s->kind == SymbolKind::Net;
}

// Whether terminal i of a primitive instance with n terminals is an input (or an inout switch terminal), as slang binds them:
// an n-input gate has one output first, an n-output gate one input last, and the others follow their port directions.
// True for a terminal that passes a z through unchanged: the data terminal of a MOS switch, either side of a
// bidirectional switch. Every gate and UDP input turns z into x.
bool passesZ(const PrimitiveInstanceSymbol& p, size_t i) {
    const std::string_view n = p.primitiveType.name;
    if (n == "nmos" || n == "pmos" || n == "rnmos" || n == "rpmos" || n == "cmos" || n == "rcmos") return i == 1;
    if (n == "tran" || n == "rtran" || n == "tranif0" || n == "tranif1" || n == "rtranif0" || n == "rtranif1") return i < 2;
    return false;
}

bool primitiveInput(const PrimitiveInstanceSymbol& p, size_t i, size_t n) {
    const PrimitiveSymbol& prim = p.primitiveType;
    if (prim.primitiveKind == PrimitiveSymbol::NInput) return i != 0;
    if (prim.primitiveKind == PrimitiveSymbol::NOutput) return i + 1 == n;
    if (i >= prim.ports.size()) return false;
    const PrimitivePortDirection d = prim.ports[i]->direction;
    return d == PrimitivePortDirection::In || d == PrimitivePortDirection::InOut;
}

// A 2-state bit holds no x or z, so a 4-state value converted to a type with one loses that bit's value (its z becomes 0).
// A packed struct or array has one where a member or the element has one.
bool hasTwoStateBit(const Type& t0) {
    const Type& t = t0.getCanonicalType();
    if (t.kind == SymbolKind::PackedStructType) {
        for (auto& f : t.as<PackedStructType>().membersOfType<FieldSymbol>())
            if (hasTwoStateBit(f.getType())) return true;
        return false;
    }
    if (t.kind == SymbolKind::PackedArrayType) return hasTwoStateBit(t.as<PackedArrayType>().elementType);
    return t.isIntegral() && !t.isFourState();
}

// ---------------------------------------------------------------------------------------------------------------
// Bits of a net that only z drives. A net bit read by an instance input and driven by nothing but constant z (counted per bit across
// every driver: part-selects, a whole-vector z under a part, member selects, concatenated z bits) floats: the tri-state INFO of LNT-039
// is wrong there. Any other driver of the bit (an expression, an instance output, a switch or pull primitive, an inout connection)
// makes it a bus, and so does a driver or a drive whose bits are not static; a pulled net kind is never judged.
using DriveSite = std::pair<uint32_t, size_t>;
DriveSite siteOf(slang::SourceLocation at) { return {at.buffer().getId(), at.offset()}; }

/// The bits of `e` (LSB first) that are z whatever the design does; false for a bit something else drives, or only conditionally.
std::vector<bool> certainZBits(Lint& l, const Expression& e) {
    std::vector<bool> out;
    if (!e.type || !e.type->isIntegral() || e.type->getBitWidth() == 0 || e.type->getBitWidth() > 4096) return out;
    out.assign(e.type->getBitWidth(), false);
    if (const ConstantValue cv = l.evalElaborated(e); cv.isInteger()) {
        const SVInt& v = cv.integer();
        for (size_t i = 0; i < out.size() && i < v.getBitWidth(); ++i) out[i] = v[static_cast<int32_t>(i)].value == logic_t::z.value;
        return out;
    }
    const Expression& u = strip(e);
    if (u.kind == ExpressionKind::Concatenation) {
        size_t at = 0;
        const auto operands = u.as<ConcatenationExpression>().operands();
        for (auto it = operands.rbegin(); it != operands.rend(); ++it) {
            const Expression& operand = **it;
            const auto part = certainZBits(l, operand);
            for (size_t i = 0; i < part.size() && at + i < out.size(); ++i) out[at + i] = part[i];
            at += operand.type && operand.type->isIntegral() ? operand.type->getBitWidth() : 0;
        }
    }
    return out;
}

using FloatingDrivers = std::map<DriveSite, std::string>;

FloatingDrivers floatingZDrivers(Lint& l) {
    using Bits = std::optional<std::pair<uint64_t, uint64_t>>;
    struct Info {
        std::vector<std::pair<uint64_t, uint64_t>> reads, active;
        bool skip = false;
    };
    struct Refs : ASTVisitor<Refs, VisitFlags::AllGood> {
        Lint& l;
        std::function<void(const NetSymbol&, Bits)> cb;
        Refs(Lint& lint, std::function<void(const NetSymbol&, Bits)> f) : l(lint), cb(std::move(f)) {}
        void chain(const Expression& e, const std::function<void()>& descend) {
            const Expression* x = &e;
            for (;;) {
                if (x->kind == ExpressionKind::ElementSelect) x = &strip(x->as<ElementSelectExpression>().value());
                else if (x->kind == ExpressionKind::RangeSelect) x = &strip(x->as<RangeSelectExpression>().value());
                else if (x->kind == ExpressionKind::MemberAccess) x = &strip(x->as<MemberAccessExpression>().value());
                else break;
            }
            if (x->kind != ExpressionKind::NamedValue || x->as<NamedValueExpression>().symbol.kind != SymbolKind::Net) {
                descend();
                return;
            }
            Bits bits;
            if (x != &e) {
                ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
                EvalContext ec(ac);
                ValuePath path(e, ec);
                if (path.lsp && path.isFullyStatic()) bits = path.lspBounds;
            }
            else if (const auto w = x->type->getBitstreamWidth(); w != 0) bits = std::pair<uint64_t, uint64_t>{0, w - 1};
            cb(x->as<NamedValueExpression>().symbol.as<NetSymbol>(), bits);
        }
        void handle(const NamedValueExpression& e) {
            chain(e, [&] { visitDefault(e); });
        }
        void handle(const ElementSelectExpression& e) {
            chain(e, [&] { visitDefault(e); });
        }
        void handle(const RangeSelectExpression& e) {
            chain(e, [&] { visitDefault(e); });
        }
        void handle(const MemberAccessExpression& e) {
            chain(e, [&] { visitDefault(e); });
        }
    };
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const NetSymbol*, Info> nets;
        explicit V(Lint& lint) : l(lint) {}
        void claim(const Expression& e, bool read) {
            Refs refs(l, [&](const NetSymbol& n, Bits bits) {
                auto& info = nets[&n];
                if (!bits) {   // a read at a run-time index proves no bit floats; a drive at one may be any bit
                    if (!read) info.skip = true;
                    return;
                }
                (read ? info.reads : info.active).push_back(*bits);
            });
            e.visit(refs);
        }
        void handle(const InstanceSymbol& inst) {
            for (auto* pc : inst.getPortConnections()) {
                const Expression* e = pc ? pc->getExpression() : nullptr;
                if (!e) continue;
                const bool input = pc->port.kind == SymbolKind::Port && pc->port.as<PortSymbol>().direction == ArgumentDirection::In;
                claim(e->kind == ExpressionKind::Assignment ? e->as<AssignmentExpression>().left() : *e, input);
            }
            const InstanceBodySymbol& body = inst.getCanonicalBody() ? *inst.getCanonicalBody() : inst.body;
            body.visit(*this);
        }
        void handle(const PrimitiveInstanceSymbol& prim) {   // a switch, gate or pull on a net drives or pulls the bits it names
            for (const auto* e : prim.getPortConnections())
                if (e) claim(e->kind == ExpressionKind::Assignment ? e->as<AssignmentExpression>().left() : *e, false);
        }
    } v(l);
    l.visitHardware(v);

    FloatingDrivers out;
    auto* an = l.context().design.analysis.get();
    if (!an) return out;
    for (auto& [net, info] : v.nets) {
        if (info.skip || info.reads.empty()) continue;
        if (const auto kind = net->netType.netKind; kind != slang::ast::NetType::Wire && kind != slang::ast::NetType::Tri) continue;
        const auto* body = net->getParentScope() ? net->getParentScope()->asSymbol().as_if<InstanceBodySymbol>() : nullptr;
        if (!body || (net->getDeclaredType() && net->getDeclaredType()->getInitializer())) continue;
        bool port = false;
        for (const auto* pl : body->getPortList())
            if (pl->kind != SymbolKind::Port || pl->as<PortSymbol>().internalSymbol == net) port = true;
        const uint64_t width = net->getType().getBitstreamWidth();
        if (port || width == 0 || width > 4096) continue;
        std::vector<uint32_t> zs(width, 0), actives(width, 0);
        std::vector<std::vector<DriveSite>> zSites(width);
        bool unknown = false;
        for (const auto* d : an->getDrivers(*net)) {
            if (d->path.empty() || !d->path.isFullyStatic()) {
                unknown = true;
                break;
            }
            const auto [lo, hi] = d->getBounds();
            if (hi >= width) {
                unknown = true;
                break;
            }
            std::vector<bool> zb;
            const Expression* left = nullptr;
            if (d->containingSymbol && d->containingSymbol->kind == SymbolKind::ContinuousAssign) {
                const auto& assignment = d->containingSymbol->as<ContinuousAssignSymbol>().getAssignment();
                if (assignment.kind == ExpressionKind::Assignment) {
                    left = &assignment.as<AssignmentExpression>().left();
                    zb = certainZBits(l, assignment.as<AssignmentExpression>().right());
                }
            }
            for (uint64_t b = lo; b <= hi; ++b) {
                if (left && b - lo < zb.size() && zb[b - lo]) {
                    ++zs[b];
                    zSites[b].push_back(siteOf(left->sourceRange.start()));
                }
                else
                    ++actives[b];
            }
        }
        if (unknown) continue;
        for (const auto& [lo, hi] : info.active)
            for (uint64_t b = lo; b <= hi && b < width; ++b) ++actives[b];
        std::vector<uint64_t> bits;
        for (const auto& [lo, hi] : info.reads)
            for (uint64_t b = lo; b <= hi && b < width; ++b)
                if (zs[b] > 0 && actives[b] == 0 && std::find(bits.begin(), bits.end(), b) == bits.end()) bits.push_back(b);
        if (bits.empty()) continue;
        std::sort(bits.begin(), bits.end());
        std::string text;
        for (auto b : bits) text += (text.empty() ? "" : ", ") + std::string(net->name) + "[" + std::to_string(b) + "]";
        const std::string what = "net bit " + text + " that an instance input reads: nothing but 'z drives it, so it floats";
        for (auto b : bits)
            for (const auto& site : zSites[b]) out.emplace(site, what);
    }
    return out;
}

// X/Z hazards. One visitor serves two rules: LNT-039 (X/Z written into synthesizable values) and LNT-040 (X/Z
// compared, case-matched or used as a condition, and the === family). Final blocks and assertions are INFO.
enum class Nearest { None, Assign, Cond, Compare };

struct XZVisit : HardwareVisitor<XZVisit, VisitFlags::AllCanonical> {
    Lint& l;
    bool compare;                         // LNT-040 mode; false = LNT-039 mode
    bool inFinal = false, inAssert = false;
    Nearest nearest = Nearest::None;      // innermost enclosing assignment / conditional arm / comparison
    bool lhsNet = false;                  // the enclosing assignment or declaration writes only nets (see netTarget)
    // An operator, conversion that changes the value, call or index lies between the assigned value and the node visited.
    // Such a 'z is X in the result (`a & 1'bz`, `~HIZ`), so it is not a tri-state. Reset where a value is assigned.
    bool throughOp = false;
    uint32_t keep = UINT32_MAX;           // low bits that reach the target after an enclosing narrowing
    // Where an all-'z value ends up when nothing else can drive it: an `input` port of an instance, or a net whose only driver this is.
    // Such a 'z is no tri-state bus: the input floats.
    const PortSymbol* zInput = nullptr;
    const NetSymbol* zNet = nullptr;
    // The continuous assignment whose value is being visited (its target's start), and the bits that only z drives (floatingZDrivers).
    slang::SourceLocation curDrive;
    std::optional<FloatingDrivers> floating;

    XZVisit(Lint& lint, bool cmpMode) : l(lint), compare(cmpMode) {}

    // The finding for a 'z that reaches a net unchanged and drives a bit nothing else drives while an instance input reads it, or null.
    const std::string* floatingRead() {
        if (compare || nearest != Nearest::Assign || !curDrive.valid()) return nullptr;
        if (!floating) floating = floatingZDrivers(l);
        const auto it = floating->find(siteOf(curDrive));
        return it == floating->end() ? nullptr : &it->second;
    }

    void emit(slang::SourceLocation at, const std::string& msg, Severity base, Confidence conf, std::string nextStep) {
        if (!l.isUser(at)) return;
        l.count();
        l.report(at, msg,
                 {.severity = inFinal || inAssert ? Severity::Info : base, .confidence = conf, .nextStep = std::move(nextStep)});
    }

    // LNT-039: X/Z literal in an assignment or a conditional arm. The tri-state INFO is for a 'z that reaches the net unchanged.
    void literal(const Expression& e) {
        if (compare || (nearest != Nearest::Assign && nearest != Nearest::Cond)) return;
        bool hasX, hasZ;
        if (!xzLiteral(e, hasX, hasZ)) return;
        const bool everyBitZ = e.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral ||
                               (e.kind == ExpressionKind::IntegerLiteral && allZ(e.as<IntegerLiteral>().getValue(), keep));
        if (hasZ && !hasX && everyBitZ && lhsNet && !throughOp && zInput)
            emit(e.sourceRange.start(), "'z drives input port '" + std::string(zInput->name) + "' of an instance: nothing else drives it, so the input floats", Severity::Warning,
                 Confidence::Established, "Drive the input with a defined value; a floating input is X in simulation and undefined in hardware.");
        else if (hasZ && !hasX && everyBitZ && lhsNet && !throughOp && zNet)
            emit(e.sourceRange.start(), "'z drives net '" + std::string(zNet->name) + "', which has no other driver and is no port: nothing can drive it, so it floats", Severity::Warning,
                 Confidence::Established, "Drive the net with a defined value, or connect it to a port or a bus another driver shares.");
        else if (hasZ && !hasX && everyBitZ && lhsNet && !throughOp && floatingRead())
            emit(e.sourceRange.start(), "'z drives " + *floatingRead(), Severity::Warning, Confidence::Established,
                 "Drive the bit with a defined value, or give the net another driver or a pull.");
        else if (hasZ && !hasX && everyBitZ && lhsNet && !throughOp)
            emit(e.sourceRange.start(), "'z drives a net (tri-state)", Severity::Info, Confidence::Established,
                 "Confirm the tri-state is intended.");
        else
            emit(e.sourceRange.start(), "X/Z literal assigned to a synthesizable value", Severity::Warning, Confidence::Established,
                 "Replace the X/Z with a defined value unless a don't-care is intended.");
    }
    // A named constant (parameter, localparam or enum value) whose value has X/Z bits, reaching an assigned value.
    bool assigned() const { return !compare && (nearest == Nearest::Assign || nearest == Nearest::Cond); }
    static const Symbol* constantOfSymbol(const Symbol& s) {
        return s.kind == SymbolKind::Parameter || s.kind == SymbolKind::EnumValue ? &s : nullptr;
    }
    // `b.P` through an interface port: a member access whose member is the constant (the port is only the way in).
    static bool isConstantMember(const Expression& e) {
        return e.kind == ExpressionKind::MemberAccess && constantOfSymbol(e.as<MemberAccessExpression>().member) != nullptr;
    }
    // The named constant at the root of a chain of selects and member accesses (P.t[0][1:0], b.P[1]), or null. A reference
    // is plain (NamedValue), hierarchical (HierarchicalValue) or through an interface port (isConstantMember).
    static const Symbol* constantRoot(const Expression& e) {
        const Expression* x = &e;
        for (;;) {
            if (ValueExpressionBase::isKind(x->kind)) return constantOfSymbol(x->as<ValueExpressionBase>().symbol);
            switch (x->kind) {
                case ExpressionKind::ElementSelect: x = &x->as<ElementSelectExpression>().value(); break;
                case ExpressionKind::RangeSelect: x = &x->as<RangeSelectExpression>().value(); break;
                case ExpressionKind::MemberAccess:
                    if (isConstantMember(*x)) return constantOfSymbol(x->as<MemberAccessExpression>().member);
                    x = &x->as<MemberAccessExpression>().value();
                    break;
                default: return nullptr;
            }
        }
    }
    // The whole named constant (a reference), not a select of it.
    static bool isReference(const Expression& e) {
        return ValueExpressionBase::isKind(e.kind) || isConstantMember(e);
    }
    static const ConstantValue& constantOf(const Symbol& s) {
        return s.kind == SymbolKind::Parameter ? s.as<ParameterSymbol>().getValue() : s.as<EnumValueSymbol>().getValue();
    }
    static std::string constantWord(const Symbol& s) { return s.kind == SymbolKind::EnumValue ? "enum value" : "parameter"; }
    static std::string constantFix(const Symbol& s) { return "Replace the X/Z in the " + constantWord(s) + " with a defined value unless a don't-care is intended."; }

    // An index is not constant for this check when slang does not fold it, or when it calls a user subroutine. slang folds a
    // call by running it, so a static local (or a static variable the call reads) would give the value of one run, not the
    // value the design gets: such an index may reach any bit.
    bool runtimeIndex(const Expression& index) const { return hasUserCall(index) || !l.evalElaborated(index); }
    // A select in the chain has a user call in its index. Such a chain is not folded at any level, since slang's fold runs the call.
    static bool chainCalls(const Expression& e) {
        for (const Expression* x = &e;;) {
            switch (x->kind) {
                case ExpressionKind::ElementSelect:
                    if (hasUserCall(x->as<ElementSelectExpression>().selector())) return true;
                    x = &x->as<ElementSelectExpression>().value();
                    break;
                case ExpressionKind::RangeSelect:
                    if (hasUserCall(x->as<RangeSelectExpression>().left()) || hasUserCall(x->as<RangeSelectExpression>().right()))
                        return true;
                    x = &x->as<RangeSelectExpression>().value();
                    break;
                case ExpressionKind::MemberAccess: x = &x->as<MemberAccessExpression>().value(); break;
                default: return false;
            }
        }
    }
    // X/Z bits that `e` (a reference to the named constant `root`, or a chain of selects over it) may select. A select with a
    // constant index folds to the bits it selects, when its chain folds. A runtime index (see runtimeIndex) may reach any bit
    // of the object it selects from, so that object decides, at any level of the chain, and `byIndex` says so. A chain whose
    // value does not evaluate here is judged as the whole constant.
    bool holdsXZ(const Expression& e, const Symbol& root, bool& byIndex) const {
        if (!chainCalls(e))
            if (ConstantValue cv = l.evalElaborated(e)) return cv.hasUnknown();
        const Expression* base = nullptr;
        if (e.kind == ExpressionKind::ElementSelect) {
            base = &e.as<ElementSelectExpression>().value();
            if (runtimeIndex(e.as<ElementSelectExpression>().selector())) byIndex = true;
        }
        else if (e.kind == ExpressionKind::RangeSelect) {
            base = &e.as<RangeSelectExpression>().value();
            if (runtimeIndex(e.as<RangeSelectExpression>().left()) || runtimeIndex(e.as<RangeSelectExpression>().right()))
                byIndex = true;
        }
        else if (e.kind == ExpressionKind::MemberAccess)
            base = &e.as<MemberAccessExpression>().value();
        if (base) return holdsXZ(*base, root, byIndex);
        return constantOf(root).hasUnknown();
    }

    // A reference to, or a select or member access of, a named constant. The whole chain is judged by the bits it selects;
    // a non-constant index says so, since it may reach the X/Z bits.
    void judgeSelect(const Expression& e, const Symbol& root) {
        if (!assigned() || !constantOf(root).hasUnknown()) return;   // no X/Z bit anywhere in the constant: none in a select
        bool byIndex = false;
        if (!holdsXZ(e, root, byIndex)) return;
        const std::string what = constantWord(root) + " '" + std::string(root.name) + "'";
        // An X-free value with 'z bits driving a net is a tri-state, as a 'z literal is (INFO). The bits judged are the whole
        // constant or the bits a constant select picks; a runtime index may reach an X bit, so it stays a WARNING. An operator
        // between the constant and the net makes its bits X, so that stays a WARNING too.
        if (!byIndex && !throughOp && lhsNet) {
            const ConstantValue v = isReference(e) ? constantOf(root) : l.evalElaborated(e);
            if (v.isInteger() && allZ(v.integer(), keep)) {
                if (zInput) {
                    emit(e.sourceRange.start(), "'z of " + what + " drives input port '" + std::string(zInput->name) + "' of an instance: nothing else drives it, so the input floats",
                         Severity::Warning, Confidence::Established,
                         "Drive the input with a defined value; a floating input is X in simulation and undefined in hardware.");
                    return;
                }
                if (zNet) {
                    emit(e.sourceRange.start(), "'z of " + what + " drives net '" + std::string(zNet->name) + "', which has no other driver and is no port: nothing can drive it, so it floats",
                         Severity::Warning, Confidence::Established,
                         "Drive the net with a defined value, or connect it to a port or a bus another driver shares.");
                    return;
                }
                if (const auto* floats = floatingRead())
                    emit(e.sourceRange.start(), "'z of " + what + " drives " + *floats, Severity::Warning, Confidence::Established,
                         "Drive the bit with a defined value, or give the net another driver or a pull.");
                else
                    emit(e.sourceRange.start(), "'z of " + what + " drives a net (tri-state)", Severity::Info, Confidence::Established,
                         "Confirm the tri-state is intended.");
                return;
            }
        }
        if (isReference(e))
            emit(e.sourceRange.start(), what + " with X/Z bits is assigned to a synthesizable value", Severity::Warning,
                 Confidence::Established, constantFix(root));
        else if (byIndex)
            emit(e.sourceRange.start(),
                 what + " with X/Z bits is selected by a non-constant index and assigned to a synthesizable value: the index may reach the X/Z bits",
                 Severity::Warning, Confidence::Established, constantFix(root));
        else
            emit(e.sourceRange.start(), "selected bits of " + what + " include X/Z and are assigned to a synthesizable value",
                 Severity::Warning, Confidence::Established, constantFix(root));
    }
    // LNT-040: the first X/Z literal, or reference to a named constant whose selected bits hold X/Z, in an expression. A select
    // chain over a named constant is judged as one value (P[0] with only P[1] unknown holds none), so only its index
    // expressions are searched further.
    struct XZFinder : ASTVisitor<XZFinder, VisitFlags::AllGood> {
        const XZVisit* owner;
        const Expression* hit = nullptr;
        bool runtimeCondition = false;
        const Symbol* constant = nullptr;   // set when the hit is a named constant, null for a literal
        explicit XZFinder(const XZVisit* o) : owner(o) {}
        void selectors(const Expression& e) {
            for (const Expression* x = &e;;) {
                switch (x->kind) {
                    case ExpressionKind::ElementSelect:
                        x->as<ElementSelectExpression>().selector().visit(*this);
                        x = &x->as<ElementSelectExpression>().value();
                        break;
                    case ExpressionKind::RangeSelect:
                        x->as<RangeSelectExpression>().left().visit(*this);
                        x->as<RangeSelectExpression>().right().visit(*this);
                        x = &x->as<RangeSelectExpression>().value();
                        break;
                    case ExpressionKind::MemberAccess:
                        if (isConstantMember(*x)) return;
                        x = &x->as<MemberAccessExpression>().value();
                        break;
                    default: return;
                }
            }
        }
        template <typename T>
            requires std::derived_from<T, Expression>
        void handle(const T& e0) {
            if (hit) return;
            const Expression& e = e0;
            if (const Symbol* root = constantRoot(e)) {
                bool byIndex = false;
                if (constantOf(*root).hasUnknown() && owner->holdsXZ(e, *root, byIndex)) {
                    hit = &e;
                    constant = root;
                }
                else
                    selectors(e);
                return;
            }
            if (isXZ(e)) {
                hit = &e;
                return;
            }
            // X/Z bits do not pass through a conversion to a 2-state type, nor through a system call that returns a 2-state
            // property of its argument ($bits, $countones, $clog2, $size, ...). $signed, $unsigned and 4-state casts carry them.
            if (e.kind == ExpressionKind::Conversion && !e.type->isFourState()) return;
            if (e.kind == ExpressionKind::Call && e.as<CallExpression>().isSystemCall()) {
                static const std::set<std::string_view> twoState = {
                    "$bits", "$countones", "$countbits", "$onehot", "$onehot0", "$isunknown", "$clog2", "$size", "$left", "$right",
                    "$low", "$high", "$increment", "$dimensions", "$unpacked_dimensions", "$typename"};
                if (twoState.count(e.as<CallExpression>().getSubroutineName())) return;
            }
            this->visitDefault(e0);
        }
    };
    XZFinder findXZOrConstant(const Expression& e) const {
        XZFinder f(this);
        e.visit(f);
        return f;
    }
    // Boolean conversion is indeterminate only when no known one decides the condition.
    XZFinder conditionConstant(const Expression& cond) const {
        XZFinder f(this);
        const Expression& e = strip(cond);
        auto value = l.evalElaborated(e);
        if (!value.isInteger() || (!value.isTrue() && !value.isFalse())) f = findXZOrConstant(e);
        f.runtimeCondition = !value.isInteger();
        return f;
    }
    // An X/Z literal or named constant in either bound of a `[lo:hi]` item (the other items of an inside set are wildcards).
    XZFinder rangeBoundXZ(const Expression& item) const {
        XZFinder f(this);
        if (item.kind != ExpressionKind::ValueRange) return f;
        const auto& r = item.as<ValueRangeExpression>();
        f = findXZOrConstant(r.left());
        if (!f.hit) f = findXZOrConstant(r.right());
        return f;
    }
    // How a finding names what it found: the literal, or the named constant.
    static std::string xzWhat(const XZFinder& f) {
        return f.constant ? constantWord(*f.constant) + " '" + std::string(f.constant->name) + "' holding X/Z bits" : "an X/Z literal";
    }

    // The chain is judged as one value, so none of its inner selects is visited on its own; the index expressions are.
    void visitSelectors(const Expression& e) {
        Scoped<bool> o(throughOp, true);   // an index is an operand, not the value selected
        for (const Expression* x = &e;;) {
            switch (x->kind) {
                case ExpressionKind::ElementSelect:
                    x->as<ElementSelectExpression>().selector().visit(*this);
                    x = &x->as<ElementSelectExpression>().value();
                    break;
                case ExpressionKind::RangeSelect:
                    x->as<RangeSelectExpression>().left().visit(*this);
                    x->as<RangeSelectExpression>().right().visit(*this);
                    x = &x->as<RangeSelectExpression>().value();
                    break;
                case ExpressionKind::MemberAccess:
                    if (isConstantMember(*x)) return;
                    x = &x->as<MemberAccessExpression>().value();
                    break;
                default: return;
            }
        }
    }
    // A reference to a named constant: plain (NamedValue), hierarchical (HierarchicalValue) or through an interface port
    // (a member access, below). Its selects and member accesses are judged the same way.
    void handle(const NamedValueExpression& e) {
        if (const Symbol* root = constantRoot(e)) judgeSelect(e, *root);
    }
    void handle(const HierarchicalValueExpression& e) {
        if (const Symbol* root = constantRoot(e)) judgeSelect(e, *root);
    }
    void handle(const ElementSelectExpression& e) {
        if (const Symbol* root = constantRoot(e)) {
            judgeSelect(e, *root);
            visitSelectors(e);
        }
        else {
            e.value().visit(*this);
            Scoped<bool> o(throughOp, true);   // the index picks the value; it is not the value
            e.selector().visit(*this);
        }
    }
    void handle(const RangeSelectExpression& e) {
        if (const Symbol* root = constantRoot(e)) {
            judgeSelect(e, *root);
            visitSelectors(e);
        }
        else {
            e.value().visit(*this);
            Scoped<bool> o(throughOp, true);
            e.left().visit(*this);
            e.right().visit(*this);
        }
    }
    // Operators: the operand of one is not the value that reaches the net unchanged, so a 'z below it is X.
    void handle(const UnaryExpression& e) {
        Scoped<bool> o(throughOp, true);
        this->visitDefault(e);
    }
    void handle(const StreamingConcatenationExpression& e) {
        Scoped<bool> o(throughOp, true);
        this->visitDefault(e);
    }
    // A replication repeats its operand unchanged (transparent); its count is an operand of its own.
    void handle(const ReplicationExpression& e) {
        {
            Scoped<bool> o(throughOp, true);
            e.count().visit(*this);
        }
        e.concat().visit(*this);
    }
    // A conversion changes the value that reaches its result when it changes the width (an explicit one; a sign-only cast does
    // not), or when a 4-state value goes to a type with a 2-state bit (`bit'(1'bz)` is 0, not z). An implicit widening is
    // transparent only when it sign-extends: zero extension drives the new upper bits 0, not z (`w2 = 1'bz` drives 2'b0z).
    // An unbased unsized 'z fills every bit, and sign extension repeats z. `ctxSigned` is the signedness of the expression the
    // extension happens in: a `?:` arm extends in the type of the whole conditional (`s ? 2'sbzz : 8'b0` zero-extends), while an
    // assignment, argument or port extends by its operand's own signedness (`w = 4'sbz` sign-extends, as simulators do).
    void convert(const ConversionExpression& c, bool ctxSigned) {
        const Type& to = *c.type;
        const Type& from = *c.operand().type;
        bool changes = false;
        const bool integral = to.isIntegral() && from.isIntegral();
        if (!c.isImplicit())
            changes = !(integral && to.getBitWidth() == from.getBitWidth());
        else if (integral && to.getBitWidth() > from.getBitWidth())
            // Zero fill changes the value only if a filled bit reaches the target: `oe ? 0 : 1'bz` into one bit widens the
            // 'z arm to 32 bits and keeps bit 0 only.
            changes = strip(c.operand()).kind != ExpressionKind::UnbasedUnsizedIntegerLiteral && !(from.isSigned() && ctxSigned) &&
                      keep > from.getBitWidth();
        if (from.isFourState() && hasTwoStateBit(to)) changes = true;
        // Narrowing keeps the low bits: a tri-state survives only if every kept bit is z (2'bz0 into 1 bit drives 0). A
        // constant operand is judged here; otherwise the kept width goes down to each literal or constant it holds (each
        // arm of `oe ? d : 'bz` is judged on its own kept bits).
        uint32_t kept = keep;
        if (integral && !changes && to.getBitWidth() < from.getBitWidth()) {
            const ConstantValue v = l.evalElaborated(c.operand());
            if (v.isInteger()) changes = !allZ(v.integer(), to.getBitWidth());
            else kept = std::min(keep, static_cast<uint32_t>(to.getBitWidth()));
        }
        Scoped<uint32_t> k(keep, kept);
        Scoped<bool> o(throughOp, throughOp || changes);
        this->visitDefault(c);
    }
    void handle(const ConversionExpression& c) { convert(c, true); }
    void handle(const MemberAccessExpression& e) {
        if (const Symbol* root = constantRoot(e)) {
            judgeSelect(e, *root);
            visitSelectors(e);
        }
        else
            this->visitDefault(e);
    }

    // The value an input or inout port takes from its connection (an inout's 'z is tri-state, as for a net).
    void inputPortValue(const PortSymbol& port, const Expression& e, bool connected = false) {
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<bool> s(lhsNet, (port.internalSymbol ? port.internalSymbol : &port)->kind == SymbolKind::Net);
        Scoped<bool> t(throughOp, false);
        // an expression wired to a plain input is the only thing that drives it; an inout is a pad that others may drive
        Scoped<const PortSymbol*> z(zInput, connected && port.direction == ArgumentDirection::In ? &port : nullptr);
        Scoped<const NetSymbol*> zn(zNet, nullptr);
        Scoped<slang::SourceLocation> drive(curDrive, slang::SourceLocation());
        e.visit(*this);
    }
    static bool isInput(const PortSymbol& port) {
        return port.direction == ArgumentDirection::In || port.direction == ArgumentDirection::InOut;
    }

    // An input or inout port connection is a continuous assignment of the actual to the port, so its value is an
    // assigned value. An output connection drives the actual, not a value written. An unconnected port connects its
    // default expression, so that default is checked here too. A top-level instance has no connection at all: the
    // default of its input port is the value it takes.
    void handle(const InstanceSymbol& inst) {
        for (auto* pc : inst.getPortConnections()) {
            const Expression* e = pc ? pc->getExpression() : nullptr;
            if (!e) continue;
            const bool input = !compare && pc->port.kind == SymbolKind::Port && isInput(pc->port.as<PortSymbol>());
            if (input)
                inputPortValue(pc->port.as<PortSymbol>(), *e, true);
            else {
                Scoped<Nearest> n(nearest, Nearest::None);
                e->visit(*this);
            }
        }
        if (inst.isTopLevel() && !compare)
            for (auto* sym : inst.body.getPortList())
                if (sym->kind == SymbolKind::Port && isInput(sym->as<PortSymbol>()))
                    if (auto* init = sym->as<PortSymbol>().getInitializer()) inputPortValue(sym->as<PortSymbol>(), *init);
        const InstanceBodySymbol& body = inst.getCanonicalBody() ? *inst.getCanonicalBody() : inst.body;
        body.visit(*this);
    }
    // A gate or user-defined primitive drives its input terminals like continuous assignments to nets, so an input terminal's
    // value is an assigned value (a plain 'z there is the tri-state). An output terminal is a target and writes no value.
    void handle(const PrimitiveInstanceSymbol& p) {
        const auto conns = p.getPortConnections();
        for (size_t i = 0; i < conns.size(); i++) {
            const Expression* e = conns[i];
            if (!e) continue;
            if (!compare && primitiveInput(p, i, conns.size())) {
                Scoped<Nearest> n(nearest, Nearest::Assign);
                Scoped<bool> net(lhsNet, true);
                // A gate or UDP reads a z input as x (IEEE 1800 sections 28, 29): only a switch's data terminal passes z.
                Scoped<bool> t(throughOp, !passesZ(p, i));
                e->visit(*this);
            }
            else {
                Scoped<Nearest> n(nearest, Nearest::None);
                e->visit(*this);
            }
        }
    }
    void handle(const ReturnStatement& r) {
        if (r.expr) {
            Scoped<Nearest> n(nearest, Nearest::Assign);
            Scoped<bool> t(throughOp, false);
            r.expr->visit(*this);
        }
    }
    // A user task or function call. An input or inout argument is assigned to its formal, so its value is an assigned value
    // wherever the call sits; slang binds an argument left out as its default, so a used default is checked too. An output
    // or ref argument is a target, not a value. A system call keeps the default visit.
    void handle(const CallExpression& c) {
        // a function that returns an all-z constant hands the net the same 'z a literal would
        if (!compare && !c.isSystemCall() && assigned() && lhsNet && !throughOp && c.arguments().empty()) {
            const ConstantValue v = l.evalElaborated(c);
            if (v.isInteger() && allZ(v.integer(), keep)) {
                const std::string what = "call of function '" + std::string(c.getSubroutineName()) + "'";
                if (zInput)
                    emit(c.sourceRange.start(), "'z of " + what + " drives input port '" + std::string(zInput->name) + "' of an instance: nothing else drives it, so the input floats",
                         Severity::Warning, Confidence::Established, "Drive the input with a defined value; a floating input is X in simulation and undefined in hardware.");
                else if (zNet)
                    emit(c.sourceRange.start(), "'z of " + what + " drives net '" + std::string(zNet->name) + "', which has no other driver and is no port: nothing can drive it, so it floats",
                         Severity::Warning, Confidence::Established, "Drive the net with a defined value, or connect it to a port or a bus another driver shares.");
                else if (const auto* floats = floatingRead())
                    emit(c.sourceRange.start(), "'z of " + what + " drives " + *floats, Severity::Warning, Confidence::Established,
                         "Drive the bit with a defined value, or give the net another driver or a pull.");
                else
                    emit(c.sourceRange.start(), "'z of " + what + " drives a net (tri-state)", Severity::Info, Confidence::Established, "Confirm the tri-state is intended.");
            }
        }
        Scoped<bool> o(throughOp, true);   // a call is an operator: its result is not the argument's value
        if (c.isSystemCall()) {
            this->visitDefault(c);
            return;
        }
        const SubroutineSymbol& sub = *std::get<const SubroutineSymbol*>(c.subroutine);
        if (const Expression* receiver = c.thisClass()) receiver->visit(*this);
        const auto formals = sub.getArguments();
        const auto args = c.arguments();
        for (size_t i = 0; i < args.size(); i++) {
            if (!args[i]) continue;
            const FormalArgumentSymbol* formal = i < formals.size() ? formals[i] : nullptr;
            const bool value = formal && (formal->direction == ArgumentDirection::In || formal->direction == ArgumentDirection::InOut);
            Scoped<Nearest> n(nearest, value ? Nearest::Assign : Nearest::None);
            Scoped<bool> s(lhsNet, false);   // a formal is a variable
            args[i]->visit(*this);
        }
    }
    void handle(const UnbasedUnsizedIntegerLiteral& e) { literal(e); }
    void handle(const IntegerLiteral& e) { literal(e); }

    void handle(const AssignmentExpression& a) {
        { Scoped<Nearest> n(nearest, Nearest::None); a.left().visit(*this); }
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<bool> s(lhsNet, netTarget(a.left()));
        Scoped<bool> t(throughOp, false);
        Scoped<const PortSymbol*> z(zInput, nullptr);
        Scoped<const NetSymbol*> zn(zNet, floatingNet(a.left()));
        Scoped<slang::SourceLocation> drive(curDrive, a.left().sourceRange.start());
        a.right().visit(*this);
    }
    // The net a continuous assignment writes, when this assignment is its only driver and it is no port of its module, nor wired to
    // an inout port of an instance (a pad): nothing else can drive it, so a 'z written to it is a floating net, not a bus.
    const NetSymbol* floatingNet(const Expression& left) {
        const Expression* x = &strip(left);
        while (true) {
            if (x->kind == ExpressionKind::ElementSelect) x = &strip(x->as<ElementSelectExpression>().value());
            else if (x->kind == ExpressionKind::RangeSelect) x = &strip(x->as<RangeSelectExpression>().value());
            else break;
        }
        if (x->kind != ExpressionKind::NamedValue) return nullptr;
        // the bits this assignment writes, when the select is static: only the drivers of those bits count
        std::optional<std::pair<uint64_t, uint64_t>> bits;
        if (&strip(left) != x) {
            slang::ast::ASTContext ac(l.compilation().getRoot(), slang::ast::LookupLocation::max);
            slang::ast::EvalContext ec(ac);
            slang::ast::ValuePath path(left, ec);
            if (path.lsp && path.isFullyStatic()) bits = path.lspBounds;
        }
        return floatingNet(x->as<NamedValueExpression>().symbol, bits);
    }
    const NetSymbol* floatingNet(const Symbol& sym, std::optional<std::pair<uint64_t, uint64_t>> bits = std::nullopt) {
        if (sym.kind != SymbolKind::Net) return nullptr;
        // a net that is pulled when released (tri0, tri1) or keeps its charge (trireg) does not float: INFO, not a floating input
        if (const auto kind = sym.as<NetSymbol>().netType.netKind; kind == slang::ast::NetType::Tri0 || kind == slang::ast::NetType::Tri1 || kind == slang::ast::NetType::TriReg)
            return nullptr;
        const auto* body = sym.getParentScope() ? sym.getParentScope()->asSymbol().as_if<InstanceBodySymbol>() : nullptr;
        if (!body) return nullptr;
        for (const auto* port : body->getPortList())
            if (port->kind == SymbolKind::Port && port->as<PortSymbol>().internalSymbol == &sym) return nullptr;
            else if (port->kind != SymbolKind::Port) return nullptr;   // an interface or multi-port: not judged
        auto* an = l.context().design.analysis.get();
        // a declaration assignment (`wire w = 1'bz;`) is the net's driver, though the analysis may not list it: with one, the net has no
        // other driver when it lists none or only that one
        const bool declared = sym.as<NetSymbol>().getDeclaredType() && sym.as<NetSymbol>().getDeclaredType()->getInitializer();
        if (!an) return nullptr;
        size_t drivers = 0;
        for (const auto* d : an->getDrivers(sym.as<ValueSymbol>())) {
            if (bits) {
                const auto b = d->getBounds();
                if (b.second < bits->first || b.first > bits->second) continue;   // a driver of other bits
            }
            drivers++;
        }
        if (drivers != (declared ? 0u : 1u)) {
            if (!(declared && drivers == 1)) return nullptr;
        }
        struct Pads : ASTVisitor<Pads, VisitFlags::AllGood> {
            const Symbol& net;
            bool found = false;
            explicit Pads(const Symbol& n) : net(n) {}
            void handle(const InstanceSymbol& inst) {
                for (auto* pc : inst.getPortConnections()) {
                    if (!pc || !pc->getExpression() || pc->port.kind != SymbolKind::Port) continue;
                    if (pc->port.as<PortSymbol>().direction == ArgumentDirection::In) continue;
                    pc->getExpression()->visitSymbolReferences([&](const Expression&, const Symbol& used) { found = found || &used == &net; });
                }
            }
            void handle(const PrimitiveInstanceSymbol& prim) {   // a switch or pull gate on the net: not judged
                for (auto* e : prim.getPortConnections())
                    if (e) e->visitSymbolReferences([&](const Expression&, const Symbol& used) { found = found || &used == &net; });
            }
        } pads(sym);
        body->visit(pads);
        return pads.found ? nullptr : &sym.as<NetSymbol>();
    }
    void handle(const ContinuousAssignSymbol& ca) {
        Scoped<Nearest> n(nearest, Nearest::None);
        ca.getAssignment().visit(*this);
    }
    void handle(const VariableSymbol& v) {
        const Expression* init = v.getDeclaredType() ? v.getDeclaredType()->getInitializer() : nullptr;
        if (!init) return;
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<bool> s(lhsNet, false);
        Scoped<bool> t(throughOp, false);
        init->visit(*this);
    }
    void handle(const NetSymbol& v) {
        const Expression* init = v.getDeclaredType() ? v.getDeclaredType()->getInitializer() : nullptr;
        if (!init) return;
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<bool> s(lhsNet, true);
        Scoped<bool> t(throughOp, false);
        Scoped<const PortSymbol*> z(zInput, nullptr);
        Scoped<const NetSymbol*> zn(zNet, floatingNet(v));
        Scoped<slang::SourceLocation> drive(curDrive, slang::SourceLocation());
        init->visit(*this);
    }
    void handle(const ProceduralBlockSymbol& p) {
        Scoped<bool> f(inFinal, inFinal || p.procedureKind == ProceduralBlockKind::Final);
        this->visitDefault(p);
    }
    void handle(const ImmediateAssertionStatement& s) {
        Scoped<bool> a(inAssert, true);
        this->visitDefault(s);
    }
    void handle(const ConcurrentAssertionStatement& s) {
        Scoped<bool> a(inAssert, true);
        this->visitDefault(s);
    }
    // The value of a parameter or localparam declaration, and of a parameter override, is not a hardware assignment: LNT-039
    // reports an X where the parameter is used as a value (its use site), not in the declaration. LNT-040 keeps its comparisons.
    void handle(const ParameterSymbol& p) {
        if (compare) this->visitDefault(p);
    }

    void handle(const ConditionalExpression& c) {
        if (compare)
            for (auto& cond : c.conditions) {
                if (isXZ(*cond.expr) && oneBitLiteral(strip(*cond.expr)))
                    emit(cond.expr->sourceRange.start(), "X/Z literal selects a conditional operator: result is X in simulation",
                         Severity::Warning, Confidence::Established, "Use a defined select value.");
                else if (const XZFinder f = conditionConstant(*cond.expr); f.hit)
                    emit(cond.expr->sourceRange.start(), xzWhat(f) + (f.runtimeCondition
                             ? " may reach a conditional-operator select: simulation and synthesis may disagree"
                             : " selects a conditional operator: result is X in simulation"),
                         Severity::Warning, Confidence::Established, "Use a defined select value.");
            }
        {   // a condition selects, it is not the value written: X/Z there is LNT-040's
            Scoped<Nearest> sel(nearest, compare ? nearest : Nearest::None);
            for (auto& cond : c.conditions) cond.expr->visit(*this);
        }
        Scoped<Nearest> n(nearest, nearest == Nearest::Compare ? Nearest::Compare : Nearest::Cond);
        visitArm(c.left(), c.type->isSigned());
        visitArm(c.right(), c.type->isSigned());
    }
    // An arm is converted to the type of the whole conditional, so its extension follows that type's signedness.
    void visitArm(const Expression& arm, bool ctxSigned) {
        if (arm.kind == ExpressionKind::Conversion) convert(arm.as<ConversionExpression>(), ctxSigned);
        else arm.visit(*this);
    }

    void handle(const BinaryExpression& b) {
        // Relational operators compare too: an X/Z literal in them is LNT-040's, as for ==.
        const bool relational = b.op == BinaryOperator::LessThan || b.op == BinaryOperator::LessThanEqual ||
                                b.op == BinaryOperator::GreaterThan || b.op == BinaryOperator::GreaterThanEqual;
        bool cmp = relational || b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality ||
                   b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality ||
                   b.op == BinaryOperator::WildcardEquality || b.op == BinaryOperator::WildcardInequality;
        if (compare) {
            if (b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality)
                emit(b.sourceRange.start(), "'===' / '!==' are not synthesizable (4-state equality)", Severity::Warning,
                     Confidence::Established, "Use '==' / '!=' on defined values.");
            else if (b.op == BinaryOperator::WildcardEquality || b.op == BinaryOperator::WildcardInequality) {
                // `==?` takes X/Z bits of its right operand as wildcards, and a constant pattern is what the operator is for. A right
                // operand that is not a constant is a four-state signal whose X/Z bits would be the wildcards: simulation matches on
                // them, and a synthesis tool builds a plain equality or rejects the construct.
                if (!l.evalElaborated(b.right()))
                    emit(b.right().sourceRange.start(),
                         "'" + std::string(b.op == BinaryOperator::WildcardEquality ? "==?" : "!=?") + "' with a right operand that is not a constant: its X/Z bits act as wildcards only in simulation",
                         Severity::Warning, Confidence::Established, "Compare against a constant pattern, or mask the bits explicitly: (a & m) == (p & m).");
            }
            else if (relational || b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality) {
                XZFinder f = findXZOrConstant(b.left());
                if (!f.hit) f = findXZOrConstant(b.right());
                if (f.hit)
                    emit(b.sourceRange.start(),
                         "comparison with " + xzWhat(f) + ": unknown bits can affect simulation matching and synthesis may treat them as don't-cares",
                         Severity::Warning, Confidence::Established, "Compare against a defined value, or use casez with '?' items.");
            }
        }
        Scoped<Nearest> n(nearest, cmp ? Nearest::Compare : nearest);
        Scoped<bool> o(throughOp, true);
        this->visitDefault(b);
    }

    void handle(const InsideExpression& e) {
        XZFinder f(this);
        // X/Z bits of a plain set element are don't-care (wildcard equality, IEEE 1800-2017 11.4.13): the left operand counts, and
        // so does a bound of a range `[lo:hi]`, which is a relational comparison.
        if (compare) {
            f = findXZOrConstant(e.left());
            for (auto* r : e.rangeList())
                if (!f.hit) f = rangeBoundXZ(*r);
        }
        if (f.hit)
            emit(e.sourceRange.start(), "'inside' with " + xzWhat(f) + ": the match is X in simulation and ignored by synthesis",
                 Severity::Warning, Confidence::Established, "Use defined values in the set.");
        Scoped<Nearest> n(nearest, Nearest::Compare);
        this->visitDefault(e);
    }

    void handle(const CaseStatement& c) {
        if (compare) {
            const auto value = l.evalElaborated(c.expr);
            const auto f = value.isInteger() && !value.hasUnknown() ? XZFinder(this) : findXZOrConstant(c.expr);
            if (f.hit)
                emit(c.expr.sourceRange.start(), "case selector with " + xzWhat(f) + ": simulation matching can differ from synthesis",
                     Severity::Warning, Confidence::Established, "Use a defined selector value.");
        }
        { Scoped<Nearest> n(nearest, Nearest::None); c.expr.visit(*this); }
        for (auto& g : c.items) {
            if (compare && c.condition == CaseStatementCondition::Normal)
                for (auto* e : g.expressions) {
                    const XZFinder f = findXZOrConstant(*e);
                    if (f.hit)
                        emit(e->sourceRange.start(), "case item with " + xzWhat(f) + " never matches a defined selector value and is ignored by synthesis",
                             Severity::Warning, Confidence::Established, "Use casez/casex with '?' for don't-care bits, or a defined value.");
                }
            if (compare && c.condition == CaseStatementCondition::Inside)
                for (auto* e : g.expressions)
                    if (const XZFinder f = rangeBoundXZ(*e); f.hit)
                        emit(e->sourceRange.start(), "'case inside' range bound with " + xzWhat(f) + ": the range test is X in simulation and ignored by synthesis",
                             Severity::Warning, Confidence::Established, "Use defined range bounds.");
            Scoped<Nearest> n(nearest, Nearest::Compare);
            for (auto* e : g.expressions) e->visit(*this);
            g.stmt->visit(*this);
        }
        if (c.defaultCase) {
            c.defaultCase->visit(*this);
        }
    }

    // `case ... matches`: a plain (not casez or casex) constant pattern with an X/Z bit is a case item with that value.
    void handle(const PatternCaseStatement& c) {
        { Scoped<Nearest> n(nearest, Nearest::None); c.expr.visit(*this); }
        for (auto& g : c.items) {
            {
                Scoped<Nearest> n(nearest, Nearest::Compare);
                if (g.pattern->kind == PatternKind::Constant) {
                    const Expression& pe = g.pattern->as<ConstantPattern>().expr;
                    const XZFinder f = findXZOrConstant(pe);
                    if (compare && c.condition == CaseStatementCondition::Normal && f.hit)
                        emit(pe.sourceRange.start(), "case item with " + xzWhat(f) + " never matches a defined selector value and is ignored by synthesis",
                             Severity::Warning, Confidence::Established, "Use casez/casex with '?' for don't-care bits, or a defined value.");
                    pe.visit(*this);
                }
                else
                    g.pattern->visit(*this);
                if (g.filter) g.filter->visit(*this);
            }
            g.stmt->visit(*this);
        }
        if (c.defaultCase) c.defaultCase->visit(*this);
    }

    void handle(const ConditionalStatement& s) {
        if (compare)
            for (auto& c : s.conditions) {
                if (isXZ(*c.expr) && oneBitLiteral(strip(*c.expr)))
                    emit(c.expr->sourceRange.start(), "if condition is an X/Z literal: never taken in simulation and removed by synthesis",
                         Severity::Warning, Confidence::Established, "Use a defined condition.");
                else if (const XZFinder f = conditionConstant(*c.expr); f.hit)
                    emit(c.expr->sourceRange.start(),
                         "if condition " + (f.runtimeCondition
                             ? "may receive " + xzWhat(f) + ": simulation and synthesis may disagree"
                             : "is " + xzWhat(f) + ": never taken in simulation and removed by synthesis"),
                         Severity::Warning, Confidence::Established, "Use a defined condition.");
            }
        this->visitDefault(s);
    }
};

class XZAssignRule final : public LintRule {
public:
    XZAssignRule() : LintRule("LNT-039") {}

private:
    void check(Lint& l) override { visitDesign<XZVisit>(l, false); }
};

class XZCompareRule final : public LintRule {
public:
    XZCompareRule() : LintRule("LNT-040", "", true) {}   // slang casez-with-x is mapped here too

private:
    void check(Lint& l) override { visitDesign<XZVisit>(l, true); }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintSynthRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<NonSynthRule>(NonSynthRule::Family::Timing));
    v.push_back(std::make_unique<NonSynthRule>(NonSynthRule::Family::ProceduralOverride));
    v.push_back(std::make_unique<NonSynthRule>(NonSynthRule::Family::DataType));
    v.push_back(std::make_unique<SystemTaskRule>());
    for (auto k : {ResetCodingRule::ResetOnly, ResetCodingRule::NoResetValue, ResetCodingRule::NoResetBlock})
        v.push_back(std::make_unique<ResetCodingRule>(k));
    v.push_back(std::make_unique<BothEdgesRule>());
    v.push_back(std::make_unique<MixedResetStyleRule>());
    v.push_back(std::make_unique<MixedClockEdgeRule>());
    v.push_back(std::make_unique<UntestedEdgeControlRule>());
    v.push_back(std::make_unique<DataDrivenEventRule>());
    for (auto k : {AsyncResetControlRule::Polarity, AsyncResetControlRule::NonConstant})
        v.push_back(std::make_unique<AsyncResetControlRule>(k));
    v.push_back(std::make_unique<XZAssignRule>());
    v.push_back(std::make_unique<XZCompareRule>());
    return v;
}

} // namespace sverity
