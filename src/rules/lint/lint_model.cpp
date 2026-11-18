// Native lint on the shared model and the AST: clock/reset coupling (LNT-027), FSM reachability obligations (LNT-029),
// initialisation assumptions (LNT-048), memories (LNT-049), X-sensitive control (LNT-054)
// and sequential races (LNT-060).
#include <algorithm>
#include <functional>
#include <deque>
#include <map>
#include <optional>
#include <limits>
#include <set>
#include <vector>

#include "analysis/guards.h"
#include "domains/model.h"
#include "rules/lint/loop_facts.h"
#include "rules/lint/value_range.h"
#include "rules/lint/write_ranges.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/AllSyntax.h"

namespace sverity {
void reportOutputPortOverlaps(lint::Lint& l);   // lint_struct.cpp: LNT-009 for instance outputs

namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;

void timingEdges(const TimingControl& t, int& edges) {
    if (t.kind == TimingControlKind::SignalEvent) { if (t.as<SignalEventControl>().edge != EdgeKind::None) edges++; }
    else if (t.kind == TimingControlKind::EventList) for (auto* e : t.as<EventListControl>().events) timingEdges(*e, edges);
}
/// Number of edge events of an always block (0 = not clocked). `reset` = more than one edge event (clock + async control).
int edgeCount(const ProceduralBlockSymbol& p) {
    const Statement& b = p.getBody();
    int n = 0;
    if (b.kind == StatementKind::Timed) timingEdges(b.as<TimedStatement>().timing, n);
    return n;
}
bool isClocked(const ProceduralBlockSymbol& p) {
    return p.procedureKind == ProceduralBlockKind::AlwaysFF || (p.procedureKind == ProceduralBlockKind::Always && edgeCount(p) > 0);
}

// Slang stores an ANSI output-variable initializer on the public port. All
// initialization consumers must consult the same declaration fact.
const Expression* declarationInitializer(const VariableSymbol& v) {
    if (const auto* init = v.getInitializer()) return init;
    if (const auto* body = enclosingBody(v))
        for (const auto* symbol : body->getPortList())
            if (const auto* port = symbol->as_if<PortSymbol>(); port && port->isAnsiPort &&
                port->direction == ArgumentDirection::Out && port->internalSymbol == &v)
                return port->getInitializer();
    return nullptr;
}

// ----------------------------------------------------------------------------------------------------------------
// LNT-027: a clock net used as data; LNT-072: a reset net steering a clock net
class CouplingRule final : public LintRule {
public:
    enum Kind { ClockAsData, ResetSteersClock };
    explicit CouplingRule(Kind k) : LintRule(k == ClockAsData ? "LNT-027" : "LNT-072"), kind_(k) {}
    Kind kind_;
    void check(Lint& l) override {
        auto& m = model::getModel(l.context().design);
        std::set<model::NodeId> clocks, resets;
        for (auto& d : m.domains) clocks.insert(d.root);
        for (auto& f : m.flops) for (auto& r : f.resets) resets.insert(r.root.node);
        std::set<model::NodeId> opaquePorts;
        for (const auto& cell : m.blackBoxes)
            for (const auto& port : cell.ports)
                if (port.dir != model::PortDir::Out)
                    opaquePorts.insert(port.nodes.begin(), port.nodes.end());
        // `dataSink`: the node reaches a register's data input or an output port. `logicSink`: some such sink is reached through logic (a
        // register data input, or a path with an edge that is not a plain copy); an output reached by plain copies alone only forwards it.
        // A node written by a conditional (or mixed) assignment is not a plain copy of any of its sources: the output is a mux or a latch.
        std::set<model::NodeId> mixedTargets;
        for (const auto& e : m.edges) if (e.shared && e.type != model::EdgeType::AsyncCtl) mixedTargets.insert(e.to);
        auto reachesClock = [&](model::NodeId from, bool& dataSink, bool& unknown, bool* logicSink = nullptr) {
            std::vector<std::pair<model::NodeId, bool>> st{{from, false}};
            std::set<std::pair<model::NodeId, bool>> seen{{from, false}};
            bool clockReached = false;
            while (!st.empty()) {
                auto [n, viaLogic] = st.back(); st.pop_back();
                unknown = unknown || opaquePorts.count(n) || (n < m.unresolvedDriven.size() && m.unresolvedDriven[n]);
                for (auto ei : m.out[n]) {
                    const auto& e = m.edges[ei];
                    if (e.type == model::EdgeType::AsyncCtl) continue;
                    if (e.type == model::EdgeType::Seq) { dataSink = true; if (logicSink) *logicSink = true; continue; }
                    if (clocks.count(e.to) && e.to != from) { clockReached = true; continue; }
                    const bool through = viaLogic || e.type != model::EdgeType::Comb || !e.exact || e.ctrl || e.shared || mixedTargets.count(e.to);
                    if (seen.insert({e.to, through}).second) st.push_back({e.to, through});
                }
                if (n < m.topOutput.size() && m.topOutput[n] && n != from) {
                    dataSink = true;
                    if (logicSink && viaLogic) *logicSink = true;
                }
            }
            return clockReached;
        };
        for (auto c : clocks) {
            if (kind_ != ClockAsData) break;
            l.count();
            bool data = false, unknown = false, logic = false;
            reachesClock(c, data, unknown, &logic);
            if (unknown) {
                l.reportAt({}, "Unresolved: clock " + m.nameOf(c) + " reaches an opaque or approximate dependency; data use cannot be excluded",
                           {.confidence = Confidence::Unresolved, .signals = {m.nameOf(c)}});
                continue;
            }
            if (!data) continue;
            const model::ProcInfo* where = nullptr;
            for (auto ei : m.out[c]) { auto& e = m.edges[ei]; if (e.type != model::EdgeType::AsyncCtl) { where = &m.procs[e.proc]; break; } }
            if (!logic) {
                l.reportAt(where ? where->loc : SourceLocation{}, "clock " + m.nameOf(c) + " is forwarded to an output port by a plain copy: clock distribution, not data use",
                           {.severity = Severity::Info,
                            .nextStep = "No action unless the consumer samples this output as data; constrain it as a generated or forwarded clock.",
                            .hierarchy = where ? where->instancePath : std::string{}, .signals = {m.nameOf(c)}});
                continue;
            }
            l.reportAt(where ? where->loc : SourceLocation{}, "clock " + m.nameOf(c) + " is used as data (it reaches the data input of a register or an output)",
                       {.nextStep = "Clocks should only drive clock pins and clock-gating logic; sample clock-derived information with a synchronised register.",
                        .hierarchy = where ? where->instancePath : std::string{}, .signals = {m.nameOf(c)}});
        }
        for (auto r : resets) {
            if (kind_ != ResetSteersClock) break;
            bool data = false, unknown = false;
            bool clock = reachesClock(r, data, unknown);
            if (unknown) {
                l.reportAt({}, "Unresolved: reset " + m.nameOf(r) + " reaches an opaque or approximate dependency; clock coupling cannot be excluded",
                           {.confidence = Confidence::Unresolved, .signals = {m.nameOf(r)}});
                continue;
            }
            if (!clock) continue;
            l.count();
            l.reportAt({}, "reset " + m.nameOf(r) + " feeds the logic that generates a clock: a reset must not gate a clock",
                       {.nextStep = "Keep clock generation independent of reset logic.", .signals = {m.nameOf(r)}});
        }
    }
};

// Does an assignment outside a state `case` still move an arm of it? Not when it is a hold (`nx = st`, `st <= st`), when it sits in the
// other branch of an if that holds the case (the reset branch of `if (!rst_n) st <= IDLE; else case (st)`), or when an enclosing
// condition requires the state to be another value than the arm's (`if (go && st == DONE) nx = DONE;`).
struct WithinStatement : ASTVisitor<WithinStatement, VisitFlags::AllGood> {
    const CaseStatement* c;
    const AssignmentExpression* a;
    bool hasCase = false, hasAssign = false;
    WithinStatement(const CaseStatement* cs, const AssignmentExpression* as) : c(cs), a(as) {}
    void handle(const CaseStatement& s) { hasCase = hasCase || &s == c; visitDefault(s); }
    void handle(const AssignmentExpression& x) { hasAssign = hasAssign || &x == a; visitDefault(x); }
};

const Expression& withoutConversions(const Expression& e) {
    const Expression* x = &e;
    while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
    return *x;
}

bool isHold(const AssignmentExpression& a, const Symbol* state) {
    if (a.isCompound()) return false;
    const auto* source = withoutConversions(a.right()).getSymbolReference();
    return source && withoutConversions(a.right()).kind == ExpressionKind::NamedValue &&
           (source == state || source == a.left().getSymbolReference());
}

// Whether a condition can be true and can be false when the state holds a given value. The condition is read as a boolean formula over
// atoms: a comparison of the state with a constant (decided by the value), a comparison of another symbol with a constant, a bare
// symbol, any other expression (a free atom, equal expressions are one atom). Atoms are otherwise independent, so a contradiction
// the formula holds by itself (`go && !go`, `x == 1 && x == 2`) is seen and a guard that merely looks like one is not.
struct GuardTruth {
    bool canBeTrue = true, canBeFalse = true;
    bool read = true;   // false when the formula has too many atoms to enumerate: both outcomes are then assumed possible
    bool undecided = false;   // an atom reads the state (or what is derived from it) in a form that is not evaluated per state
};

// What the state machine's guards may read of the state: `tainted` holds the state, its next-state variables and every net or variable
// assigned from them (through continuous assignments, procedural assignments and instance ports); `single` the right side of a net or
// variable that has one continuous assignment and nothing else, which a guard reads through (`assign in_idle = st == IDLE`).
struct GuardEnv {
    std::set<const Symbol*> tainted;
    std::map<const Symbol*, const Expression*> single;
};

class GuardFormula {
public:
    GuardFormula(Lint& lint, const Symbol* state, uint64_t value, const GuardEnv* env = nullptr) : l_(lint), state_(state), value_(value), env_(env) {}
    GuardTruth truth(const Expression& e) {
        const int root = build(e);
        GuardTruth t;
        t.undecided = undecided_;
        t.canBeTrue = t.canBeFalse = false;
        if (atoms_.size() > 12) { t.read = false; t.canBeTrue = t.canBeFalse = true; return t; }
        const uint32_t count = 1u << atoms_.size();
        for (uint32_t m = 0; m < count; ++m) {
            if (!consistent(m)) continue;
            (value(root, m) ? t.canBeTrue : t.canBeFalse) = true;
        }
        if (!t.canBeTrue && !t.canBeFalse) t.canBeTrue = t.canBeFalse = true;   // no consistent assignment: nothing is decided
        return t;
    }

private:
    struct Node { enum Kind { Const, Atom, Not, And, Or } kind = Const; bool c = false; int atom = -1, a = -1, b = -1; };
    struct Atom { const Symbol* sym = nullptr; std::optional<uint64_t> eq; const Expression* expr = nullptr; };
    Lint& l_;
    const Symbol* state_;
    uint64_t value_;
    const GuardEnv* env_;
    bool undecided_ = false;
    std::vector<Node> nodes_;
    std::vector<Atom> atoms_;

    bool taintedSymbol(const Symbol* s) const { return env_ && s && env_->tainted.count(s); }
    // An atom this formula leaves free: if it reads the state or anything derived from it, its value per state is not known.
    void noteFree(const Expression& e) {
        if (!env_) return;
        Reads r;
        e.visit(r);
        for (const auto* sym : r.syms) if (taintedSymbol(sym)) undecided_ = true;
    }
    // The value of an expression that reads only the state (and constants, and nets that are one assignment of such an expression),
    // for the state value of this formula: `st inside {A, B}`, `st < RUN`, `st[1]`, a wire equal to `st == IDLE`.
    bool bind(slang::ast::EvalContext& ec, const Expression& e, int depth, std::set<const Symbol*>& bound) {
        Reads r;
        e.visit(r);
        for (const auto* sym : r.syms) {
            if (bound.count(sym)) continue;
            const auto kind = sym->kind;
            if (kind == SymbolKind::Parameter || kind == SymbolKind::EnumValue || kind == SymbolKind::Specparam || kind == SymbolKind::Genvar) continue;
            const auto* value = sym->as_if<ValueSymbol>();
            if (!value) return false;
            if (sym == state_) {
                const auto& type = value->getType();
                ec.createLocal(value, slang::ConstantValue(slang::SVInt(type.getBitWidth(), value_, type.isSigned())));
            } else {
                if (depth > 4 || !env_) return false;
                const auto it = env_->single.find(sym);
                if (it == env_->single.end()) return false;
                if (!bind(ec, *it->second, depth + 1, bound)) return false;
                auto v = it->second->eval(ec);
                if (v.bad() || !v.isInteger() || v.hasUnknown()) return false;
                ec.createLocal(value, std::move(v));
            }
            bound.insert(sym);
        }
        return true;
    }
    std::optional<bool> perState(const Expression& e) {
        if (!env_) return std::nullopt;
        slang::ast::ASTContext ac(l_.compilation().getRoot(), slang::ast::LookupLocation::max);
        slang::ast::EvalContext ec(ac);
        std::set<const Symbol*> bound;
        if (!bind(ec, e, 0, bound)) return std::nullopt;
        const auto v = e.eval(ec);
        if (v.bad() || !v.isInteger() || v.hasUnknown()) return std::nullopt;
        return v.isTrue();
    }

    int add(Node n) { nodes_.push_back(n); return static_cast<int>(nodes_.size()) - 1; }
    int constant(bool c) { Node n; n.kind = Node::Const; n.c = c; return add(n); }
    int atom(const Atom& a) {
        for (size_t i = 0; i < atoms_.size(); ++i)
            if (atoms_[i].sym == a.sym && atoms_[i].eq == a.eq && (a.sym || (atoms_[i].expr && a.expr && atoms_[i].expr->isEquivalentTo(*a.expr))))
                { Node n; n.kind = Node::Atom; n.atom = static_cast<int>(i); return add(n); }
        atoms_.push_back(a);
        Node n; n.kind = Node::Atom; n.atom = static_cast<int>(atoms_.size()) - 1;
        return add(n);
    }
    std::optional<uint64_t> integer(const Expression& e) const {
        const auto v = l_.evalConst(e);
        if (!v.isInteger() || v.integer().hasUnknown()) return std::nullopt;
        return v.integer().as<uint64_t>();
    }
    int build(const Expression& e0) {
        const Expression& e = withoutConversions(e0);
        if (const auto c = integer(e)) return constant(*c != 0);
        if (e.kind == ExpressionKind::UnaryOp) {
            const auto& u = e.as<UnaryExpression>();
            if (u.op == UnaryOperator::LogicalNot || (u.op == UnaryOperator::BitwiseNot && e.type && e.type->getBitWidth() == 1)) {
                Node n; n.kind = Node::Not; n.a = build(u.operand());
                return add(n);
            }
        }
        if (e.kind == ExpressionKind::BinaryOp) {
            const auto& b = e.as<BinaryExpression>();
            const bool oneBit = e.type && e.type->getBitWidth() == 1;
            if (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr ||
                (oneBit && (b.op == BinaryOperator::BinaryAnd || b.op == BinaryOperator::BinaryOr))) {
                Node n; n.kind = (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::BinaryAnd) ? Node::And : Node::Or;
                n.a = build(b.left()); n.b = build(b.right());
                return add(n);
            }
            if (b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseEquality ||
                b.op == BinaryOperator::CaseInequality) {
                const bool negate = b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseInequality;
                for (const auto& [x, k] : {std::pair{&b.left(), &b.right()}, std::pair{&b.right(), &b.left()}}) {
                    const Expression& xs = withoutConversions(*x);
                    const auto kv = integer(*k);
                    if (xs.kind != ExpressionKind::NamedValue || !kv) continue;
                    const Symbol* sym = xs.getSymbolReference();
                    int result;
                    if (sym == state_) result = constant(*kv == value_);
                    else { if (taintedSymbol(sym)) undecided_ = true; Atom a; a.sym = sym; a.eq = *kv; result = atom(a); }
                    if (!negate) return result;
                    Node n; n.kind = Node::Not; n.a = result;
                    return add(n);
                }
            }
        }
        if (e.kind == ExpressionKind::NamedValue && e.getSymbolReference() == state_) return constant(value_ != 0);
        if (const auto decided = perState(e)) return constant(*decided);
        noteFree(e);
        if (e.kind == ExpressionKind::NamedValue) {
            Atom a; a.sym = e.getSymbolReference();
            return atom(a);
        }
        Atom a; a.expr = &e;
        return atom(a);
    }
    bool value(int node, uint32_t m) const {
        const Node& n = nodes_[node];
        switch (n.kind) {
            case Node::Const: return n.c;
            case Node::Atom: return (m >> n.atom) & 1u;
            case Node::Not: return !value(n.a, m);
            case Node::And: return value(n.a, m) && value(n.b, m);
            case Node::Or: return value(n.a, m) || value(n.b, m);
        }
        return false;
    }
    // Two comparisons of one symbol with different constants are not both true; a comparison with a constant is a test of the symbol too.
    bool consistent(uint32_t m) const {
        for (size_t i = 0; i < atoms_.size(); ++i)
            for (size_t j = i + 1; j < atoms_.size(); ++j)
                if (atoms_[i].sym && atoms_[i].sym == atoms_[j].sym && atoms_[i].eq && atoms_[j].eq && *atoms_[i].eq != *atoms_[j].eq &&
                    ((m >> i) & 1u) && ((m >> j) & 1u))
                    return false;
        return true;
    }
};

struct MovesArm : ASTVisitor<MovesArm, VisitFlags::AllGood> {
    MovesArm(Lint& lint, const CaseStatement* cs, const AssignmentExpression* as, const Symbol* st, const std::set<uint64_t>& v)
        : l(lint), c(cs), a(as), state(st), armValues(v) {}
    Lint& l;
    const CaseStatement* c;
    const AssignmentExpression* a;
    const Symbol* state;
    const std::set<uint64_t>& armValues;
    bool applies = true;
    // `state == K` conjuncts of a condition that holds where the assignment runs.
    void requires_(const Expression& e0) {
        const Expression& e = withoutConversions(e0);
        if (e.kind != ExpressionKind::BinaryOp) return;
        const auto& b = e.as<BinaryExpression>();
        if (b.op == BinaryOperator::LogicalAnd) { requires_(b.left()); requires_(b.right()); return; }
        if (b.op != BinaryOperator::Equality && b.op != BinaryOperator::CaseEquality) return;
        for (const auto& [x, k] : {std::pair{&b.left(), &b.right()}, std::pair{&b.right(), &b.left()}}) {
            if (withoutConversions(*x).kind != ExpressionKind::NamedValue || withoutConversions(*x).getSymbolReference() != state) continue;
            const auto value = l.evalConst(*k);
            if (!value.isInteger() || value.integer().hasUnknown()) continue;
            if (const auto v = value.integer().as<uint64_t>(); v && !armValues.count(*v)) applies = false;
        }
    }
    void handle(const ConditionalStatement& q) {
        // A write in the other branch of an enclosing `if` still moves the state out of the arm: the branch the case sits in
        // does not run then, the state changes anyway. Only the block's own reset branch is exempt (see the caller).
        WithinStatement t(c, a);
        q.ifTrue.visit(t);
        if (t.hasAssign && !t.hasCase && q.conditions.size() == 1 && !q.conditions[0].pattern) requires_(*q.conditions[0].expr);
        visitDefault(q);
    }
};

// A state comparison `st == K` in a condition (through && and ||): the state symbols and the enumerators they are compared with.
void stateTests(const Expression& e0, std::vector<std::pair<const Symbol*, const Symbol*>>& out) {
    const Expression& e = withoutConversions(e0);
    if (e.kind != ExpressionKind::BinaryOp) return;
    const auto& b = e.as<BinaryExpression>();
    if (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) { stateTests(b.left(), out); stateTests(b.right(), out); return; }
    if (b.op != BinaryOperator::Equality && b.op != BinaryOperator::CaseEquality) return;
    for (const auto& [x, k] : {std::pair{&b.left(), &b.right()}, std::pair{&b.right(), &b.left()}}) {
        const auto& xs = withoutConversions(*x);
        const auto& ks = withoutConversions(*k);
        if (xs.kind == ExpressionKind::NamedValue && ks.kind == ExpressionKind::NamedValue && ks.getSymbolReference() &&
            ks.getSymbolReference()->kind == SymbolKind::EnumValue && xs.getSymbolReference() &&
            xs.getSymbolReference()->as_if<ValueSymbol>() && xs.getSymbolReference()->as<ValueSymbol>().getType().getCanonicalType().isEnum())
            out.push_back({xs.getSymbolReference(), ks.getSymbolReference()});
    }
}

// ----------------------------------------------------------------------------------------------------------------
// LNT-029: FSM state encodings (enum types assigned in clocked code and used as case expressions): dead states are
// reported (LNT-029); reachability and liveness are always emitted as proof obligations (LNT-071, never proven here).
class FsmRule final : public LintRule {
public:
    enum Kind { Structural, Obligation };
    explicit FsmRule(Kind k) : LintRule(k == Structural ? "LNT-029" : "LNT-071", k == Structural ? "" :
        "only scalar enum selectors linked through direct same-enum value sources to clocked state are analyzed; array/dynamic selectors and indirect or opaque alias relationships are not fully inventoried"), kind_(k) {}
    Kind kind_;
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Symbol*, std::set<const Symbol*>> edges;
        std::map<const Symbol*, std::set<uint64_t>> bySymbol;
        std::map<const Symbol*, bool> unknownBySymbol;
        std::map<const Symbol*, bool> sequentialSymbol;
        std::vector<std::pair<const CaseStatement*, const ProceduralBlockSymbol*>> cases;
        std::map<const ProceduralBlockSymbol*, std::set<const Symbol*>> procWrites;
        std::map<const CaseStatement*, std::vector<const AssignmentExpression*>> externalWrites;   // assignments of the procedure outside the case
        std::map<const CaseStatement*, const SubroutineSymbol*> inSubroutine;
        std::map<const ProceduralBlockSymbol*, std::map<const Symbol*, slang::SourceLocation>> stateChains;   // if / else-if chains that compare a state with enumerators
        std::set<const ConditionalStatement*> chainTails;
        const SubroutineSymbol* curSub = nullptr;
        std::map<const ProceduralBlockSymbol*, bool> opaqueProcedure;
        std::map<const ProceduralBlockSymbol*, std::set<const Type*>> enumWriteTypes;   // enum types written through any target form
        std::map<const ProceduralBlockSymbol*, std::set<const Type*>> enumDirectWrites;   // ... written to a scalar enum variable itself, not a member or element of one
        const ProceduralBlockSymbol* curProc = nullptr;
        // Every assignment to a scalar enum variable (the procedure is null for a continuous assignment) and every declaration initialiser.
        std::vector<std::pair<const AssignmentExpression*, const ProceduralBlockSymbol*>> enumAssigns;
        std::vector<std::pair<const Symbol*, const Expression*>> enumInits;
        // What each net or variable is assigned from, for the symbols a guard may read through: `derived[s]` are the symbols assigned from
        // `s` (a continuous or procedural assignment, a declaration initialiser, an instance output fed by an input that reads `s`);
        // `assignments[s]` counts every write of `s` and `continuous[s]` its one continuous assignment.
        std::map<const Symbol*, std::set<const Symbol*>> derived;
        std::map<const Symbol*, size_t> assignments;
        std::map<const Symbol*, const Expression*> continuous;
        explicit V(Lint& lint) : l(lint) {}
        void noteDerived(const Expression& left, const Expression& right, bool whole, bool isContinuous) {
            Reads reads;
            right.visit(reads);
            if (left.kind == ExpressionKind::Streaming) return;
            for (const auto* target : lvalueSymbols(left)) {
                assignments[target]++;
                if (isContinuous && whole && left.kind == ExpressionKind::NamedValue) continuous[target] = &right;
                for (const auto* source : reads.syms) derived[source].insert(target);
            }
        }
        void handle(const InstanceSymbol& inst) {
            if (l.isUser(inst.location)) {
                std::set<const Symbol*> inputs;
                std::vector<const Symbol*> outputs;
                for (const auto* pc : inst.getPortConnections()) {
                    const auto* x = pc ? pc->getExpression() : nullptr;
                    if (!x || pc->port.kind != SymbolKind::Port) continue;
                    const auto direction = pc->port.as<PortSymbol>().direction;
                    if (direction != ArgumentDirection::Out) { Reads r; x->visit(r); inputs.insert(r.syms.begin(), r.syms.end()); }
                    if (direction != ArgumentDirection::In) {
                        const Expression* target = x->kind == ExpressionKind::Assignment ? &x->as<AssignmentExpression>().left() : x;
                        for (const auto* sym : lvalueSymbols(*target)) { outputs.push_back(sym); assignments[sym] += 2; }   // not a single continuous assignment
                    }
                }
                for (const auto* source : inputs) derived[source].insert(outputs.begin(), outputs.end());
            }
            visitDefault(inst);
        }
        void handle(const NetSymbol& net) {
            if (l.isUser(net.location) && net.getInitializer()) {
                assignments[&net]++;
                continuous[&net] = net.getInitializer();
                Reads reads;
                net.getInitializer()->visit(reads);
                for (const auto* source : reads.syms) derived[source].insert(&net);
            }
            visitDefault(net);
        }
        GuardEnv guardEnv(const std::set<const Symbol*>& related, const Symbol* state) const {
            GuardEnv env;
            std::vector<const Symbol*> work(related.begin(), related.end());
            if (state) work.push_back(state);
            env.tainted.insert(work.begin(), work.end());
            while (!work.empty()) {
                const auto* sym = work.back();
                work.pop_back();
                const auto it = derived.find(sym);
                if (it == derived.end()) continue;
                for (const auto* target : it->second) if (env.tainted.insert(target).second) work.push_back(target);
            }
            for (const auto& [sym, rhs] : continuous) {
                const auto count = assignments.find(sym);
                if (count != assignments.end() && count->second == 1) env.single[sym] = rhs;
            }
            return env;
        }
        // A selector the relation inventory cannot name as one scalar state symbol: an array element, a path through an
        // element select, a call result or a cast.
        static bool unattributedSelector(const Expression& e) {
            const Expression* x = &e;
            for (;;) {
                if (x->kind == ExpressionKind::Conversion) { x = &x->as<ConversionExpression>().operand(); continue; }
                if (x->kind == ExpressionKind::MemberAccess) { x = &x->as<MemberAccessExpression>().value(); continue; }
                break;
            }
            if (x->kind == ExpressionKind::ElementSelect || x->kind == ExpressionKind::RangeSelect) return true;
            if (e.kind == ExpressionKind::Conversion) return true;
            const bool named = e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue ||
                               e.kind == ExpressionKind::MemberAccess;
            return !named || !scalarEnum(e.getSymbolReference());
        }
        static const Type* enumTypeOf(const Expression& e) {
            const Type& t = e.type->getCanonicalType();
            return t.isEnum() ? &t : nullptr;
        }
        static bool scalarEnum(const Symbol* s) {
            const auto* value = s ? s->as_if<ValueSymbol>() : nullptr;
            return value && value->getType().getCanonicalType().isEnum();
        }
        void noteAssign(const Expression& left, const Expression& right, bool seq) {
            const Type* t = enumTypeOf(left);
            auto* target = left.getSymbolReference();
            if (!t || !scalarEnum(target)) return;
            if (seq) sequentialSymbol[target] = true;
            collect(right, t, target);
        }
        // Dependencies point from the assigned symbol to its sources, never to downstream readers.
        void collect(const Expression& e, const Type* t, const Symbol* target) {
            auto cv = l.evalConst(e);
            if (cv.isInteger() && !cv.integer().hasUnknown()) {
                if (auto value = cv.integer().as<uint64_t>()) bySymbol[target].insert(*value);
                else unknownBySymbol[target] = true;
                return;
            }
            if (e.kind == ExpressionKind::Conversion) {
                const auto& operand = e.as<ConversionExpression>().operand();
                if (enumTypeOf(e) == t && enumTypeOf(operand) == t) collect(operand, t, target);
                else unknownBySymbol[target] = true;
                return;
            }
            if (e.kind == ExpressionKind::ConditionalOp) {
                const auto& conditional = e.as<ConditionalExpression>();
                collect(conditional.left(), t, target);
                collect(conditional.right(), t, target);
                return;
            }
            if ((e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue) &&
                enumTypeOf(e) == t && scalarEnum(e.getSymbolReference())) {
                edges[target].insert(e.getSymbolReference());
                return;
            }
            unknownBySymbol[target] = true;
        }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                Assigns a;
                p.getBody().visit(a);
                bool seq = isClocked(p);
                struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
                    bool opaque = false;
                    void handle(const CallExpression& call) { opaque = opaque || !call.isSystemCall(); visitDefault(call); }
                } calls;
                p.getBody().visit(calls);
                opaqueProcedure[&p] = calls.opaque;
                for (auto* x : a.list) {
                    noteDerived(x->left(), x->right(), !x->isCompound(), false);
                    noteAssign(x->left(), x->right(), seq);
                    if (enumTypeOf(x->left()) && scalarEnum(x->left().getSymbolReference())) enumAssigns.push_back({x, &p});
                    if (auto* sym = x->left().getSymbolReference()) procWrites[&p].insert(sym);
                    if (auto* wt = enumTypeOf(x->left())) {
                        enumWriteTypes[&p].insert(wt);
                        if (x->left().kind == ExpressionKind::NamedValue || x->left().kind == ExpressionKind::HierarchicalValue) enumDirectWrites[&p].insert(wt);
                    }
                }
            }
            auto* saved = curProc;
            curProc = &p;
            visitDefault(p);
            curProc = saved;
        }
        // A continuous assignment or an input port also gives a state its values: from a cast or a signal of another type, they are not constants.
        void handle(const ContinuousAssignSymbol& assign) {
            if (l.isUser(assign.location))
                if (const auto& expr = assign.getAssignment(); expr.kind == ExpressionKind::Assignment) {
                    const auto& a = expr.as<AssignmentExpression>();
                    noteDerived(a.left(), a.right(), true, true);
                    noteAssign(a.left(), a.right(), false);
                    if (enumTypeOf(a.left()) && scalarEnum(a.left().getSymbolReference())) enumAssigns.push_back({&a, nullptr});
                }
            visitDefault(assign);
        }
        void handle(const PortSymbol& port) {
            if (l.isUser(port.location) && port.direction != ArgumentDirection::Out && port.internalSymbol && scalarEnum(port.internalSymbol))
                unknownBySymbol[port.internalSymbol] = true;
            visitDefault(port);
        }
        void handle(const VariableSymbol& v) {
            if (l.isUser(v.location) && v.getInitializer() && isModuleLevel(v)) {
                assignments[&v] += 2;   // an initialiser beside any other write: not a continuous assignment
                Reads reads;
                v.getInitializer()->visit(reads);
                for (const auto* source : reads.syms) derived[source].insert(&v);
            }
            if (l.isUser(v.location) && v.getInitializer() && isModuleLevel(v) && v.getType().getCanonicalType().isEnum()) {
                collect(*v.getInitializer(), &v.getType().getCanonicalType(), &v);
                enumInits.push_back({&v, v.getInitializer()});
            }
            visitDefault(v);
        }
        void handle(const SubroutineSymbol& sub) {
            auto* saved = curSub;
            curSub = &sub;
            visitDefault(sub);
            curSub = saved;
        }
        // The head of an if / else-if chain that tests one state against two or more enumerators: a state machine coded without a case.
        void handle(const ConditionalStatement& q) {
            if (curProc && l.isUser(curProc->location) && !chainTails.count(&q)) {
                std::vector<std::pair<const Symbol*, const Symbol*>> tests;
                size_t links = 0;
                for (const ConditionalStatement* c = &q; c;) {
                    for (const auto& cond : c->conditions) stateTests(*cond.expr, tests);
                    links++;
                    const Statement* f = c->ifFalse;
                    while (f && f->kind == StatementKind::Block && f->as<BlockStatement>().blockKind == StatementBlockKind::Sequential &&
                           f->as<BlockStatement>().body.kind == StatementKind::Conditional) f = &f->as<BlockStatement>().body;
                    c = f && f->kind == StatementKind::Conditional ? &f->as<ConditionalStatement>() : nullptr;
                    if (c) chainTails.insert(c);
                }
                std::map<const Symbol*, std::set<const Symbol*>> byState;
                for (auto [state, value] : tests) byState[state].insert(value);
                for (auto& [state, values] : byState)
                    if (values.size() >= 2 && links >= 2) stateChains[curProc].emplace(state, q.sourceRange.start());
            }
            visitDefault(q);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                cases.push_back({&c, curProc});
                if (curSub) inSubroutine[&c] = curSub;
                if (curProc)
                    if (enumTypeOf(c.expr)) {
                        Assigns a, inside;
                        curProc->getBody().visit(a);
                        c.visitStmts(inside);
                        std::set<const AssignmentExpression*> nested(inside.list.begin(), inside.list.end());
                        for (auto* assignment : a.list)
                            if (!nested.count(assignment)) externalWrites[&c].push_back(assignment);
                    }
            }
            visitDefault(c);
        }
    };
    // State machines in a style this rule does not read are reported, never passed over: next-state logic in a function (the case
    // sits in a function that returns or outputs the state's enum type) and a state coded as an if / else-if chain without a case.
    void unanalysedStyles(Lint& l, V& v) {
        std::set<const Type*> clockedTypes;
        for (auto [symbol, sequential] : v.sequentialSymbol)
            if (sequential) clockedTypes.insert(&symbol->as<ValueSymbol>().getType().getCanonicalType());
        std::set<const SubroutineSymbol*> reportedSubs;
        for (auto [c, sub] : v.inSubroutine) {
            const Type* t = V::enumTypeOf(c->expr);
            if (!t || !clockedTypes.count(t) || !reportedSubs.insert(sub).second) continue;
            bool producesState = &sub->getReturnType().getCanonicalType() == t;
            for (const auto* arg : sub->getArguments())
                producesState = producesState || (arg->direction != ArgumentDirection::In && &arg->getType().getCanonicalType() == t);
            if (!producesState) continue;
            l.count();
            l.report(c->sourceRange.start(), "FSM next-state logic is in function '" + std::string(sub->name) + "': its transitions are not analysed here",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Write the transition case in the clocked or combinational block, or review the function's arms for stuck and dead states."});
        }
        for (auto& [proc, chains] : v.stateChains)
            for (auto& [state, at] : chains) {
                const auto* value = state->as_if<ValueSymbol>();
                if (!value || !clockedTypes.count(&value->getType().getCanonicalType())) continue;
                const bool hasCase = std::any_of(v.cases.begin(), v.cases.end(), [&](const auto& k) { return k.first->expr.getSymbolReference() == state; });
                if (hasCase) continue;
                l.count();
                l.report(at, "FSM state '" + std::string(state->name) + "' is coded as an if / else-if chain: its transitions are not analysed here",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Write the state machine as a case on the state to have its stuck and dead states checked.",
                          .signals = {std::string(state->name)}});
            }
    }

    // The reset branches of a procedure (an event-list reset, or a net `classifyReset` knows), found once per procedure.
    std::map<const ProceduralBlockSymbol*, std::vector<const Statement*>> resetBranches;
    bool resetWrite(const ProceduralBlockSymbol* proc, const AssignmentExpression& x) {
        auto it = resetBranches.find(proc);
        if (it == resetBranches.end()) {
            std::vector<const Statement*> branches;
            if (const auto sync = model::synchronousReset(*proc, *model_))
                for (const auto& b : sync->known)
                    if (b.reset && !b.compound) branches.push_back(b.reset);
            it = resetBranches.emplace(proc, std::move(branches)).first;
        }
        for (const auto* branch : it->second) {
            WithinStatement within(nullptr, &x);
            branch->visit(within);
            if (within.hasAssign) return true;
        }
        return false;
    }
    const model::Model* model_ = nullptr;

    // The values a state assignment can give: constants, a hold of the state itself, a copy of another symbol of the relation, or unknown.
    struct Targets {
        std::set<uint64_t> values;
        bool self = false, copy = false, unknown = false;
        void add(const Targets& o) { values.insert(o.values.begin(), o.values.end()); self = self || o.self; copy = copy || o.copy; unknown = unknown || o.unknown; }
    };
    void targetsOf(Lint& l, const Expression& e, const Type* t, const Symbol* state, const std::set<const Symbol*>& related, Targets& out) {
        const auto cv = l.evalConst(e);
        if (cv.isInteger() && !cv.integer().hasUnknown()) {
            if (const auto value = cv.integer().as<uint64_t>()) out.values.insert(*value); else out.unknown = true;
            return;
        }
        if (e.kind == ExpressionKind::Conversion) {
            const auto& operand = e.as<ConversionExpression>().operand();
            if (V::enumTypeOf(e) == t && V::enumTypeOf(operand) == t) targetsOf(l, operand, t, state, related, out); else out.unknown = true;
            return;
        }
        if (e.kind == ExpressionKind::ConditionalOp) {
            const auto& c = e.as<ConditionalExpression>();
            targetsOf(l, c.left(), t, state, related, out);
            targetsOf(l, c.right(), t, state, related, out);
            return;
        }
        if ((e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue) && V::enumTypeOf(e) == t) {
            const Symbol* sym = e.getSymbolReference();
            if (sym == state) out.self = true;
            else if (sym && related.count(sym)) out.copy = true;
            else out.unknown = true;
            return;
        }
        out.unknown = true;
    }

    // What the state register (or its next-state variable) can hold at a point of the procedure, for one value of the current state.
    struct Flow {
        std::set<uint64_t> values;
        bool self = false;        // the state's own value: a hold
        bool undefined = false;   // no assignment has run on some path (a combinational variable that keeps no value)
        bool unknown = false;     // an assignment this analysis does not read
        void add(const Flow& o) {
            values.insert(o.values.begin(), o.values.end());
            self = self || o.self; undefined = undefined || o.undefined; unknown = unknown || o.unknown;
        }
    };
    struct Walk {
        Lint& l;
        const Type* type;
        const Symbol* state;
        const std::set<const Symbol*>& related;
        const ProceduralBlockSymbol* proc;
        const GuardEnv* env = nullptr;
    };
    static bool writesRelation(const Walk& w, const Statement& s) {
        Assigns a;
        s.visit(a);
        return std::any_of(a.list.begin(), a.list.end(), [&](const AssignmentExpression* x) { return w.related.count(x->left().getSymbolReference()) != 0; });
    }
    // A branch whose every write of the state is a reset write is the reset, which loads an entry state: it is no transition.
    bool isResetBranch(const Walk& w, const Statement& s) {
        Assigns a;
        s.visit(a);
        bool any = false;
        for (auto* x : a.list) {
            if (!w.related.count(x->left().getSymbolReference())) continue;
            if (!resetWrite(w.proc, *x)) return false;
            any = true;
        }
        return any;
    }
    // The values the state can take after `s` runs when the state held `S` before it, from the values it can hold on entry. A guard
    // that cannot be true under `S` contributes nothing; one that cannot be false does not let the entry value through.
    Flow flowOf(const Walk& w, const Statement& s, uint64_t S, const Flow& in) {
        Flow unknown = in;
        unknown.unknown = true;
        switch (s.kind) {
            case StatementKind::Empty: case StatementKind::VariableDeclaration: return in;
            case StatementKind::Timed: return flowOf(w, s.as<TimedStatement>().stmt, S, in);
            case StatementKind::Block: {
                const auto& b = s.as<BlockStatement>();
                if (b.blockKind != StatementBlockKind::Sequential) return writesRelation(w, s) ? unknown : in;
                return flowOf(w, b.body, S, in);
            }
            case StatementKind::List: {
                Flow f = in;
                for (auto* c : s.as<StatementList>().list) f = flowOf(w, *c, S, f);
                return f;
            }
            case StatementKind::ExpressionStatement: {
                const Expression& e = s.as<ExpressionStatement>().expr;
                if (e.kind != ExpressionKind::Assignment) return in;
                const auto& x = e.as<AssignmentExpression>();
                if (!w.related.count(x.left().getSymbolReference())) return in;
                if (x.left().kind != ExpressionKind::NamedValue || x.isCompound()) return unknown;
                if (resetWrite(w.proc, x)) return in;
                Flow f;
                if (isHold(x, w.state)) f.self = true;
                else {
                    Targets t1;
                    targetsOf(w.l, x.right(), w.type, w.state, w.related, t1);
                    if (t1.unknown || t1.copy) return unknown;
                    f.values = t1.values;
                    f.self = t1.self;
                }
                return f;
            }
            case StatementKind::Conditional: {
                const auto& c = s.as<ConditionalStatement>();
                bool patterns = false;
                for (const auto& cond : c.conditions) patterns = patterns || cond.pattern;
                if (patterns) return writesRelation(w, s) ? unknown : in;
                GuardTruth truth;
                for (size_t i = 0; i < c.conditions.size(); ++i) {
                    const auto t = GuardFormula(w.l, w.state, S, w.env).truth(*c.conditions[i].expr);
                    if (t.undecided) return unknown;   // a guard that reads the state in a form not evaluated per state: the transitions are not read
                    if (i == 0) truth = t;
                    else { truth.canBeTrue = truth.canBeTrue && t.canBeTrue; truth.canBeFalse = true; }
                }
                Flow out;
                bool any = false;
                if (truth.canBeTrue && !isResetBranch(w, c.ifTrue)) { out.add(flowOf(w, c.ifTrue, S, in)); any = true; }
                if (truth.canBeFalse) {
                    if (!c.ifFalse) { out.add(in); any = true; }
                    else if (!isResetBranch(w, *c.ifFalse)) { out.add(flowOf(w, *c.ifFalse, S, in)); any = true; }
                }
                return any ? out : in;
            }
            case StatementKind::Case: {
                const auto& c = s.as<CaseStatement>();
                if (c.condition != CaseStatementCondition::Normal) return writesRelation(w, s) ? unknown : in;
                const Expression& sel = withoutConversions(c.expr);
                const bool onState = sel.kind == ExpressionKind::NamedValue && sel.getSymbolReference() == w.state;
                Flow out;
                if (onState) {
                    const Statement* hit = nullptr;
                    for (const auto& item : c.items) {
                        for (auto* e : item.expressions) {
                            const auto value = w.l.evalConst(*e);
                            if (!value.isInteger() || value.integer().hasUnknown()) return writesRelation(w, s) ? unknown : in;
                            if (value.integer().as<uint64_t>() == std::optional<uint64_t>(S)) hit = item.stmt;
                        }
                        if (hit) break;
                    }
                    if (hit) return flowOf(w, *hit, S, in);
                    return c.defaultCase ? flowOf(w, *c.defaultCase, S, in) : in;
                }
                if (w.env) {   // a selector that reads the state (or what is derived from it) other than as the state itself
                    Reads r;
                    c.expr.visit(r);
                    for (const auto* sym : r.syms) if (w.env->tainted.count(sym)) return writesRelation(w, s) ? unknown : in;
                }
                for (const auto& item : c.items) out.add(flowOf(w, *item.stmt, S, in));
                out.add(c.defaultCase ? flowOf(w, *c.defaultCase, S, in) : in);
                return out;
            }
            default: return writesRelation(w, s) ? unknown : in;
        }
    }

    // The transition graph of the state machine a case describes, read from the assignments themselves: for each enumerator, the
    // values the procedure can assign to the state when it holds that enumerator (the arm it selects, the default or the value held
    // before the case for an enumerator no item lists, and every later write whose guard can be true for it), and the states the
    // reset, an initialiser or another procedure can load. A transition is a possible assignment: a guard is read as a formula, so one
    // that can never be true gives no transition and one that is not a formula this analysis reads is a free test. `decided` is false
    // whenever a state's successors or the entry states are not all known.
    struct Graph {
        bool decided = false;
        std::vector<std::string> stuck;         // reachable, unlisted by any item, and nothing assigned from there but a hold
        std::vector<std::string> unreachable;   // not reachable from any entry state
        std::vector<std::string> holdsListed;   // reachable, listed, and every transition out of it leads back to it: terminal by design or stuck
    };
    Graph transitionGraph(Lint& l, V& v, const CaseStatement& c, const ProceduralBlockSymbol* proc, const Type* t, const Symbol* state,
                          const std::set<const Symbol*>& related) {
        Graph g;
        if (c.condition != CaseStatementCondition::Normal || !proc) return g;
        std::map<uint64_t, std::string> names;
        for (auto& ev : t->as<EnumType>().values()) {
            const auto value = ev.getValue();
            const auto n = value.isInteger() && !value.integer().hasUnknown() ? value.integer().as<uint64_t>() : std::nullopt;
            if (!n) return g;
            names[*n] = std::string(ev.name);
        }
        for (auto* sym : related) if (v.unknownBySymbol[sym]) return g;
        if (v.opaqueProcedure[proc]) return g;
        std::set<uint64_t> listed;
        for (auto& item : c.items)
            for (auto* e : item.expressions) {
                const auto value = l.evalConst(*e);
                const auto n = value.isInteger() && !value.integer().hasUnknown() ? value.integer().as<uint64_t>() : std::nullopt;
                if (!n) return g;
                listed.insert(*n);
            }
        // entry states and jumps: writes outside this procedure, and the reset of this one
        Targets entry, jumps;
        for (auto [x, xproc] : v.enumAssigns) {
            const Symbol* target = x->left().getSymbolReference();
            if (!target || !related.count(target)) continue;
            if (xproc == proc) {
                if (!resetWrite(proc, *x)) continue;   // the procedure's own writes are read below, per state
                if (x->left().kind != ExpressionKind::NamedValue || x->isCompound()) return g;
                Targets t1;
                if (isHold(*x, state)) t1.self = true; else targetsOf(l, x->right(), t, state, related, t1);
                if (t1.unknown) return g;
                entry.add(t1);
                continue;
            }
            if (x->left().kind != ExpressionKind::NamedValue || x->isCompound()) return g;
            Targets t1;
            if (isHold(*x, state)) t1.self = true; else targetsOf(l, x->right(), t, state, related, t1);
            if (t1.unknown) return g;
            const bool initialBlock = xproc && xproc->procedureKind == ProceduralBlockKind::Initial;
            if (initialBlock) { entry.add(t1); continue; }
            if (t1.copy && t1.values.empty() && !t1.self) continue;   // `state <= nxt`: the relation itself, not a source of values
            if (xproc && resetWrite(xproc, *x)) { entry.add(t1); continue; }
            jumps.add(t1);
        }
        for (auto [sym, init] : v.enumInits) {
            if (!related.count(sym)) continue;
            Targets t1;
            targetsOf(l, *init, t, state, related, t1);
            if (t1.unknown) return g;
            entry.add(t1);
        }
        if (entry.values.empty() || entry.copy) return g;
        // successors of every enumerator
        const GuardEnv env = v.guardEnv(related, state);
        const Walk walk{l, t, state, related, proc, &env};
        Flow start;
        if (isClocked(*proc)) start.self = true;   // a register keeps its value where no branch assigns it
        else start.undefined = true;
        std::map<uint64_t, std::set<uint64_t>> next;
        for (auto& [value, name] : names) {
            const Flow f = flowOf(walk, proc->getBody(), value, start);
            if (f.unknown || f.undefined) return g;
            Targets to;
            to.values = f.values;
            to.self = f.self;
            to.add(jumps);
            if (to.unknown || to.copy) return g;
            auto& n = next[value];
            n = to.values;
            if (to.self) n.insert(value);
        }
        // reachability from the entry states
        std::set<uint64_t> reach(entry.values.begin(), entry.values.end());
        std::vector<uint64_t> work(reach.begin(), reach.end());
        while (!work.empty()) {
            const auto s = work.back();
            work.pop_back();
            auto it = next.find(s);
            if (it == next.end()) continue;
            for (auto d : it->second) if (reach.insert(d).second) work.push_back(d);
        }
        g.decided = true;
        for (auto& [value, name] : names) {
            if (!reach.count(value)) { g.unreachable.push_back(name); continue; }
            bool exit = false;
            for (auto d : next[value]) exit = exit || d != value;
            if (exit) continue;
            (listed.count(value) ? g.holdsListed : g.stuck).push_back(name);
        }
        return g;
    }

    void check(Lint& l) override {
        V v(l);
        l.visitHardware(v);
        model_ = &model::getModel(l.context().design);
        if (kind_ == Structural) unanalysedStyles(l, v);
        for (auto [c, proc] : v.cases) if (auto* t = V::enumTypeOf(c->expr)) {
            auto* state = c->expr.getSymbolReference();
            std::set<const Symbol*> related, stateSymbols;   // stateSymbols: every symbol that gives a clocked register of this type its value
            size_t nearest = std::numeric_limits<size_t>::max();
            bool ambiguous = false, opaqueType = false, knownType = false;
            for (auto [root, sequential] : v.sequentialSymbol) {
                if (!sequential) continue;
                if (&root->as<ValueSymbol>().getType().getCanonicalType() != t) continue;
                knownType = true;
                std::set<const Symbol*> closure{root};
                std::vector<std::pair<const Symbol*, size_t>> work{{root, 0}};
                size_t distance = std::numeric_limits<size_t>::max();
                bool opaque = false;
                for (size_t i = 0; i < work.size(); ++i) {
                    auto [current, depth] = work[i];
                    if (current == state) distance = std::min(distance, depth);
                    opaque = opaque || v.unknownBySymbol[current];
                    for (auto* source : v.edges[current])
                        if (closure.insert(source).second) work.push_back({source, depth + 1});
                }
                opaqueType = opaqueType || opaque;
                stateSymbols.insert(closure.begin(), closure.end());
                if (distance < nearest) { nearest = distance; related = std::move(closure); ambiguous = false; }
                else if (distance != std::numeric_limits<size_t>::max() && distance == nearest) ambiguous = true;
            }
            bool writesRelation = false, writesEnumType = false;
            if (proc) for (auto* symbol : v.procWrites[proc]) {
                writesRelation = writesRelation || related.count(symbol);
            }
            // The enum variable itself, not a member of a record (`core.cmd = INV` writes data).
            writesEnumType = proc && v.enumDirectWrites[proc].count(t) != 0;
            // Transition logic that writes this enum type, whose selector the relation inventory cannot attribute to a
            // clocked state symbol (an array or dynamically selected element, a cast, or a register in another scope).
            // A procedure that writes no enum variable of this type itself and no symbol feeding a clocked register of it builds a data
            // value (a command field of a record), not a state: its case is not a state machine to attribute.
            const bool sameTypeWrite = proc && v.enumWriteTypes[proc].count(t);
            const bool writesState = proc && (v.enumDirectWrites[proc].count(t) != 0 || std::any_of(v.procWrites[proc].begin(), v.procWrites[proc].end(),
                                                         [&](const Symbol* w) { return stateSymbols.count(w) != 0; }));
            const bool unattributed = sameTypeWrite && (V::unattributedSelector(c->expr) || (knownType && related.empty() && writesState));
            if (ambiguous || unattributed || (proc && knownType && v.opaqueProcedure[proc]) ||
                ((!state || related.empty() || !writesRelation) && opaqueType && writesEnumType)) {
                l.count();
                l.report(c->sourceRange.start(), "possible enum state-transition relation is unresolved",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Make the state/next-state relation explicit or review the opaque state source."});
                continue;
            }
            if (!state || related.empty() || !writesRelation) continue;
            Assigns caseAssignments;
            c->visitStmts(caseAssignments);
            bool caseWritesRelation = false;
            for (auto* assignment : caseAssignments.list)
                caseWritesRelation = caseWritesRelation || related.count(assignment->left().getSymbolReference());
            // A decoder beside an independent state update is not a direct transition case.
            if (!caseWritesRelation) continue;
            l.count();
            std::string dead, all;
            for (auto& ev : t->as<EnumType>().values()) {
                auto cv = ev.getValue();
                auto val = cv.isInteger() ? cv.integer().as<uint64_t>() : std::nullopt;
                all += (all.empty() ? "" : ", ") + std::string(ev.name);
                bool seen = false, unknown = false;
                for (auto* sym : related) { seen = seen || v.bySymbol[sym].count(val.value_or(0)); unknown = unknown || v.unknownBySymbol[sym]; }
                if (val && !seen && !unknown)
                    dead += (dead.empty() ? "" : ", ") + std::string(ev.name);
            }
            bool relationUnknown = false;
            for (auto* sym : related) relationUnknown = relationUnknown || v.unknownBySymbol[sym];
            if (relationUnknown && kind_ == Structural)
                l.report(c->sourceRange.start(), "FSM state transition analysis is unresolved for an opaque enum value computation",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                          .proof = ProofState::NotAttempted,
                          .nextStep = "Review the computed state source or use a supported direct enum value or state alias.",
                          .signals = {std::string(state->name)}});
            if (!dead.empty())
                if (kind_ == Structural) l.report(c->sourceRange.start(), "FSM state(s) [" + dead + "] are never assigned: unreachable unless they are the power-up value",
                         {.confidence = Confidence::Heuristic, .nextStep = "Remove the state or add the missing transition.", .signals = {dead}});
            // stuck-state candidates: a case arm that selects one state and gives it no transition: no assignment to the state at all, or
            // only a hold (`nx = st`), with nothing outside the case that can move it (see MovesArm)
            for (auto& g : c->items) {
                if (g.expressions.size() != 1 || g.stmt->kind == StatementKind::Empty) continue;
                std::set<uint64_t> armValues;
                bool constantArm = true;
                for (auto* e : g.expressions) {
                    const auto value = l.evalConst(*e);
                    const auto n = value.isInteger() && !value.integer().hasUnknown() ? value.integer().as<uint64_t>() : std::nullopt;
                    if (n) armValues.insert(*n); else constantArm = false;
                }
                Assigns a;
                g.stmt->visit(a);
                bool any = false;
                // `nx = RUN` in the arm for RUN keeps the state as `nx = st` does
                auto holdsOwnValue = [&](const AssignmentExpression& x) {
                    if (armValues.size() != 1 || !constantArm || x.isCompound()) return false;
                    const auto value = l.evalConst(x.right());
                    return value.isInteger() && !value.integer().hasUnknown() && value.integer().as<uint64_t>() == std::optional<uint64_t>(*armValues.begin());
                };
                for (auto* x : a.list) any = any || (related.count(x->left().getSymbolReference()) && !isHold(*x, state) && !holdsOwnValue(*x));
                bool externalStateWrite = false;
                if (!any && proc)
                    for (const auto* x : v.externalWrites[c]) {
                        if (!related.count(x->left().getSymbolReference()) || isHold(*x, state)) continue;
                        // a write in the block's reset branch is the one exit a stuck state keeps
                        if (resetWrite(proc, *x)) continue;
                        MovesArm moves(l, c, x, state, armValues);
                        proc->getBody().visit(moves);
                        // A nonconstant arm can still move the state.
                        externalStateWrite = externalStateWrite || moves.applies || !constantArm;
                    }
                if (!any && !externalStateWrite && kind_ == Structural)
                    l.report(g.expressions[0]->sourceRange.start(), "state case arm has no transition: it assigns no state, or only holds it (a hold such as `nx = st`), and nothing outside this case can move it out",
                             {.confidence = Confidence::Heuristic, .nextStep = "Add the exit transition, or make the hold explicit.", .signals = {}});
            }
            // Where the transitions can all be read, the graph answers the obligation: a reachable state nothing leaves is reported, and so
            // is a state nothing reaches. Otherwise the question stays open.
            const Graph graph = transitionGraph(l, v, *c, proc, t, state, related);
            if (graph.decided) {
                std::string stuck, unreachable;
                for (auto& name : graph.stuck) stuck += (stuck.empty() ? "" : ", ") + name;
                for (auto& name : graph.unreachable)
                    if (std::string(", " + dead + ", ").find(", " + name + ", ") == std::string::npos) unreachable += (unreachable.empty() ? "" : ", ") + name;
                if (!stuck.empty() && kind_ == Structural)
                    l.report(c->sourceRange.start(), "FSM state(s) [" + stuck + "] can be entered but have no exit: no item lists them and the default or the value held before the case keeps the state",
                             {.severity = Severity::Warning, .confidence = Confidence::Established,
                              .nextStep = "Add an item for the state with its exit transition, or make the default move to a recovery state.", .signals = {stuck}});
                if (!unreachable.empty() && kind_ == Structural)
                    l.report(c->sourceRange.start(), "FSM state(s) [" + unreachable + "] cannot be reached from the reset or initial state through the transitions of this case",
                             {.severity = Severity::Info, .confidence = Confidence::Established,
                              .nextStep = "Remove the state or add the transition that enters it.", .signals = {unreachable}});
            }
            if (kind_ == Obligation && (!graph.decided || !graph.holdsListed.empty())) {
                std::string holds;
                for (auto& name : graph.holdsListed) holds += (holds.empty() ? "" : ", ") + name;
                l.report(c->sourceRange.start(), graph.decided
                             ? "FSM over states [" + all + "]: absence of deadlock is not decided: state(s) [" + holds + "] can be entered and only keep themselves (a terminal state by design, or a stuck one)"
                             : "FSM over states [" + all + "]: reachability of every state and absence of deadlock are not decided (a transition or entry state is not a constant this analysis reads)",
                     {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Check with formal reachability or simulation coverage.",
                      .obligation = "fsm-reachability-liveness"});
            }
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-048: registers that rely on an initial value (declaration initialiser or initial block)
class InitRule final : public LintRule {
public:
    InitRule() : LintRule("LNT-048") {}
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::string profile;
        explicit V(Lint& lint, std::string p) : l(lint), profile(std::move(p)) {}
        void note(slang::SourceLocation at, const std::string& name, const char* how) {
            if (!l.isUser(at)) return;
            l.count();
            Severity sev = profile == "fpga" ? Severity::Info : Severity::Warning;
            Confidence conf = profile == "generic" ? Confidence::Unresolved : Confidence::Established;
            l.report(at, std::string("register '") + name + "' relies on " + how + (profile == "fpga" ? " (honoured by FPGA configuration)" :
                     profile == "asic" ? ": ASIC flows have no power-on value for it" : ": valid for FPGA targets only, not for ASIC (profile not selected)"),
                     {.severity = sev, .confidence = conf, .nextStep = "Add a reset, or select --profile fpga if the init value is intended.", .signals = {name},
                      .obligation = conf == Confidence::Unresolved ? "power-on-value-target" : ""});
        }
        void handle(const VariableSymbol& v) {
            if (isModuleLevel(v) && declarationInitializer(v) && v.kind == SymbolKind::Variable) {
                bool isStatic = v.lifetime == VariableLifetime::Static;
                if (isStatic) note(v.location, std::string(v.name), "its declaration initialiser");
            }
            visitDefault(v);
        }
        void handle(const ProceduralBlockSymbol& p) {
            if (p.procedureKind == ProceduralBlockKind::Initial && l.isUser(p.location)) {
                Assigns a;
                p.getBody().visit(a);
                for (auto* x : a.list)
                    for (auto* s : lvalueSymbols(x->left()))
                        if (isModuleLevel(*s)) note(x->sourceRange.start(), std::string(s->name), "an initial block");
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override {
        V v(l, l.context().config.profile);
        l.visitHardware(v);
        std::set<std::pair<const Symbol*, const Symbol*>> calledInitials;
        for (const auto& write : model::getModel(l.context().design).proceduralWrites)
            if (write.fromCall && calledInitials.insert({write.process, write.target}).second && write.process->procedureKind == ProceduralBlockKind::Initial && l.isUser(write.process->location)) {
                if (write.target) v.note(write.process->location, std::string(write.target->name), "an initial-block subroutine write");
                else l.reportAt(write.loc, "Unresolved: " + write.reason, {.confidence = Confidence::Unresolved});
            }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-049: memories (unpacked arrays) with several write processes or combinational writes
// Processes that write a memory through a non-constant select, by memory. Slang's driver list has no driver for a loop
// it cannot unroll, so such a writer is only visible in the assignment itself.
struct NonConstantWriters : HardwareVisitor<NonConstantWriters, VisitFlags::AllCanonical> {
    slang::ast::EvalContext& ec;
    const Symbol* process = nullptr;
    std::map<const Symbol*, std::set<const Symbol*>> out;
    explicit NonConstantWriters(slang::ast::EvalContext& e) : ec(e) {}
    void noteLhs(const Expression& lhs) {
        if (!process) return;
        ValuePath::visitPaths(
            lhs, ec,
            [&](const ValuePath& p) {
                auto* root = p.rootSymbol();
                if (root && !p.empty() && !p.isFullyStatic()) out[root].insert(process);
            },
            /* skipSelectors */ true);
    }
    void handle(const AssignmentExpression& a) {
        noteLhs(a.left());
        visitDefault(a);
    }
    void handle(const ProceduralBlockSymbol& p) {
        auto* saved = process;
        process = &p;
        visitDefault(p);
        process = saved;
    }
    // Continuous assignments are visited explicitly so their targets are never skipped.
    void handle(const ContinuousAssignSymbol& ca) {
        auto* saved = process;
        process = &ca;
        auto& e = ca.getAssignment();
        if (e.kind == ExpressionKind::Assignment) noteLhs(e.as<AssignmentExpression>().left());
        visitDefault(ca);
        process = saved;
    }
};

// The clock events of a clocked process: every edge event that is not an asynchronous reset. Two processes are on one clock when they
// have the same set of them, whatever their order in the event lists.
struct ClockKey {
    std::set<std::pair<const Symbol*, int>> events;
    bool operator==(const ClockKey& o) const { return !events.empty() && events == o.events; }
};
ClockKey clockKeyOf(const ProceduralBlockSymbol& p, const model::Model& m) {
    const Statement* body = &p.getBody();
    while (body && body->kind != StatementKind::Timed) {
        if (body->kind == StatementKind::Block) body = &body->as<BlockStatement>().body;
        else if (body->kind == StatementKind::List && !body->as<StatementList>().list.empty()) body = body->as<StatementList>().list.front();
        else return {};
    }
    if (!body) return {};
    ClockKey key;
    bool opaque = false;
    std::function<void(const TimingControl&)> collect = [&](const TimingControl& t) {
        if (t.kind == TimingControlKind::EventList) { for (const auto* e : t.as<EventListControl>().events) collect(*e); return; }
        if (t.kind != TimingControlKind::SignalEvent) { opaque = true; return; }
        const auto& se = t.as<SignalEventControl>();
        const Symbol* sym = se.expr.getSymbolReference();
        if (!sym) { opaque = true; return; }
        if (const auto* value = sym->as_if<ValueSymbol>(); value && model::classifyReset(m, *value).reset) return;
        key.events.insert({sym, se.edge == EdgeKind::PosEdge ? 1 : se.edge == EdgeKind::NegEdge ? 2 : se.edge == EdgeKind::BothEdges ? 3 : 0});
    };
    collect(body->as<TimedStatement>().timing);
    return opaque ? ClockKey{} : key;
}

// The part of a memory one write can reach: the words (an index interval per unpacked dimension) and, inside a word, the bits it can touch (a
// packed part- or bit-select, or a member of a packed struct; an unpacked struct member is its own position). Two writes meet only where both
// agree, so different bits of one word are not two writers of it; the bit interval is the whole word whenever the select is not placed.
struct WordBox {
    std::vector<Interval> dims;
    std::vector<Interval> bits;   // the bits of the word the write can touch, sorted and disjoint
    bool approx = false;          // `bits` is a superset (a select not placed): a common bit in it is not proven reachable
};
constexpr Wide kEveryBit = Wide(1) << 62;

// The unpacked dimensions of a memory and the indices each holds. A dimension of unknown extent (dynamic array, queue) holds every index.
std::vector<Interval> memoryDimensions(const Type& type) {
    std::vector<Interval> out;
    const Type* cur = &type.getCanonicalType();
    while (cur->isUnpackedArray()) {
        if (cur->kind == SymbolKind::FixedSizeUnpackedArrayType) {
            const auto& a = cur->as<FixedSizeUnpackedArrayType>();
            out.push_back({a.range.lower(), a.range.upper()});
        } else {
            out.push_back({-kEveryBit, kEveryBit});
        }
        const Type* element = cur->getArrayElementType();
        if (!element) break;
        cur = &element->getCanonicalType();
    }
    return out;
}

// The environment the selects of one write are read in: the value ranges of the run-time indices, and, when one run-time variable is read by
// more than one select (or twice in one), the joint value of those variables under evaluation. A select that reads such a variable takes
// the value the joint assignment gives it, so the correlation between the selects is kept.
constexpr size_t kMaxStarts = 1 << 16;   // the most positions or joint assignments enumerated exactly
struct ReachEnv {
    ValueRanges& ranges;
    Lint& l;
    ASTContext ac;
    EvalContext ec;
    std::vector<ConstantValue*> locals;                          // the shared variables' current values, in `vars` order
    std::set<const Expression*> bound;                           // the selects that read a shared variable
    bool approx = false;                                         // a correlation that could not be enumerated: the reach is a superset
    ReachEnv(ValueRanges& r, Lint& lint) : ranges(r), l(lint), ac(lint.compilation().getRoot(), LookupLocation::max), ec(ac) { ec.pushEmptyFrame(); }
    std::optional<int64_t> evalBound(const Expression& e) {
        if (const auto k = constantInt(l, e)) return k;
        const auto cv = e.eval(ec);
        if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
        return cv.integer().as<int64_t>();
    }
};

// The indices a select can take, or nothing when their range is not known. `slice` is set for a range select.
std::optional<Interval> selectReach(const Expression& step, ReachEnv& env, bool& slice) {
    Lint& l = env.l;
    ValueRanges& ranges = env.ranges;
    const bool jointly = env.bound.count(&step) != 0;
    slice = false;
    if (step.kind == ExpressionKind::ElementSelect) {
        const auto& select = step.as<ElementSelectExpression>().selector();
        if (const auto k = jointly ? env.evalBound(select) : constantInt(l, select)) return Interval{*k, *k};
        if (jointly) return std::nullopt;
        return ranges.of(select);
    }
    if (step.kind != ExpressionKind::RangeSelect) return std::nullopt;
    slice = true;
    const auto& s = step.as<RangeSelectExpression>();
    const auto kl = jointly ? env.evalBound(s.left()) : constantInt(l, s.left()), kr = constantInt(l, s.right());
    if (s.getSelectionKind() == RangeSelectionKind::Simple) {
        if (!kl || !kr) return std::nullopt;
        return Interval{std::min(*kl, *kr), std::max(*kl, *kr)};
    }
    const auto base = kl ? std::optional<Interval>(Interval{*kl, *kl}) : jointly ? std::nullopt : ranges.of(s.left());
    if (!base || !kr) return std::nullopt;
    return s.getSelectionKind() == RangeSelectionKind::IndexedUp ? Interval{base->lo, base->hi + *kr - 1} : Interval{base->lo - *kr + 1, base->hi};
}

struct BitReach {
    std::vector<Interval> bits;
    bool approx = false;   // a superset: some select was not placed
    bool none = false;     // no stored bit: a select always outside its vector
};

std::vector<Interval> mergedSpans(std::vector<Wide> starts, Wide width) {
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    std::vector<Interval> out;
    for (const Wide s : starts) {
        if (!out.empty() && s <= out.back().hi + 1) out.back().hi = std::max(out.back().hi, s + width - 1);
        else out.push_back({s, s + width - 1});
    }
    return out;
}

// The bits inside a word that the selects from `it` (the innermost of the remaining ones last) reach, as the exact set of bits the possible
// indices name (a run-time lane index leaves holes between the lanes). A select that cannot be placed (an index with no known range, a type that
// is no packed vector or struct) leaves the bits where they are, a superset, and marks the result approximate. A word too large to enumerate
// is reported as its hull, approximate.
BitReach bitsInsideWord(std::vector<const Expression*>::const_reverse_iterator it, std::vector<const Expression*>::const_reverse_iterator end, const Type& wordType,
                        ReachEnv& env) {
    constexpr size_t kMaxWork = 1 << 22;
    const Wide wordWidth = wordType.isIntegral() ? Wide(wordType.getBitWidth()) : kEveryBit + 1;
    const std::vector<Interval> whole{{0, wordWidth - 1}};
    // the item a select has reached starts at one of `starts` (offsets in the word) and is `width` bits wide
    std::vector<Wide> starts{0};
    Wide width = wordWidth;
    bool placed = false, approx = false;
    const Type* type = &wordType.getCanonicalType();
    for (; it != end && type; ++it) {
        const Expression& step = **it;
        if (step.kind == ExpressionKind::MemberAccess) {
            const auto* field = step.as<MemberAccessExpression>().member.as_if<FieldSymbol>();
            if (!field) { approx = true; break; }
            if (type->isUnpackedStruct()) return {{{Wide(field->fieldIndex), Wide(field->fieldIndex)}}, false, false};
            if (type->kind != SymbolKind::PackedStructType) { approx = true; break; }
            for (auto& s : starts) s += Wide(field->bitOffset);
            width = field->getType().getBitWidth();
            placed = true;
            type = &field->getType().getCanonicalType();
            continue;
        }
        if (!type->isIntegral() || !type->hasFixedRange()) { approx = true; break; }
        bool slice = false;
        const auto reach = selectReach(step, env, slice);
        if (!reach) { approx = true; break; }
        const auto range = type->getFixedRange();
        const Wide elementWidth = type->isPackedArray() ? Wide(type->as<PackedArrayType>().elementType.getBitWidth()) : 1;
        const Interval clipped{std::max(reach->lo, Wide(range.lower())), std::min(reach->hi, Wide(range.upper()))};
        if (clipped.lo > clipped.hi) return {{}, false, true};   // always outside the vector: it stores no bit
        const auto offset = [&](Wide k) { return (range.isDescending() ? k - range.lower() : Wide(range.upper()) - k) * elementWidth; };
        if (starts.size() * Wide(clipped.hi - clipped.lo + 1) > Wide(kMaxWork)) {
            // too many positions to enumerate: the hull of everything reachable so far
            const Wide lo = *std::min_element(starts.begin(), starts.end()) + std::min(offset(clipped.lo), offset(clipped.hi));
            const Wide hi = *std::max_element(starts.begin(), starts.end()) + std::max(offset(clipped.lo), offset(clipped.hi)) + elementWidth - 1;
            return {{{lo, hi}}, true, false};
        }
        std::vector<Wide> next;
        for (const Wide s : starts)
            for (Wide k = clipped.lo; k <= clipped.hi; ++k) next.push_back(s + offset(k));
        std::sort(next.begin(), next.end());
        next.erase(std::unique(next.begin(), next.end()), next.end());
        if (next.size() > kMaxStarts) return {{{next.front(), next.back() + elementWidth - 1}}, true, false};
        starts = std::move(next);
        width = elementWidth;
        placed = true;
        if (slice || !type->isPackedArray()) { ++it; break; }
        type = &type->as<PackedArrayType>().elementType.getCanonicalType();
    }
    if (it != end) approx = true;   // selects remain that were not placed
    return {placed ? mergedSpans(std::move(starts), width) : whole, approx, false};
}

// The part of the memory one write reaches, its selects (`chain`, outermost first) read in `env`. Returns false, with `why`, when a select cannot be
// placed (an unpacked index whose value range is unknown). Selects are read from the memory outward; once the type under the select is no
// longer an unpacked array, the rest lies inside the word.
bool placeWrite(const std::vector<const Expression*>& chain, const VariableSymbol& memory, const std::vector<Interval>& dims, ReachEnv& env,
                std::vector<WordBox>& out, std::string& why) {
    WordBox box{dims, {{0, kEveryBit}}};
    const Type* type = &memory.getType().getCanonicalType();
    size_t dim = 0;
    auto it = chain.rbegin();
    bool wholeWords = false;
    for (; it != chain.rend() && dim < dims.size() && type->isUnpackedArray(); ++it) {
        const Expression& step = **it;
        bool slice = false;
        const auto reach = selectReach(step, env, slice);
        if (!reach) {
            why = step.kind == ExpressionKind::ElementSelect || step.kind == ExpressionKind::RangeSelect
                      ? "the value range of an index of the write is not known" : "an assignment target form the scan does not decompose";
            return false;
        }
        const Interval clipped{std::max(reach->lo, dims[dim].lo), std::min(reach->hi, dims[dim].hi)};
        if (clipped.lo > clipped.hi) return true;   // always outside the array: it writes no word
        box.dims[dim] = clipped;
        ++dim;
        if (slice) { wholeWords = true; ++it; break; }
        const Type* element = type->getArrayElementType();
        if (!element) { wholeWords = true; ++it; break; }
        type = &element->getCanonicalType();
    }
    if (!wholeWords && dim == dims.size()) {
        auto reach = bitsInsideWord(it, chain.rend(), *type, env);
        if (reach.none) return true;   // an out-of-range packed lane stores no bit: the write reaches no word
        box.bits = std::move(reach.bits);
        box.approx = reach.approx || env.approx;
    }
    if (env.approx) box.approx = true;
    out.push_back(std::move(box));
    return true;
}


// The parts of the memory that every leaf of the assignment target `lhs` naming `memory` can reach, added to `out`. Returns false, with `why`, when a
// leaf cannot be placed: an unpacked index whose value range is unknown, or a target form the scan does not decompose. Selects are read from the
// memory outward; once the type under the select is no longer an unpacked array, the rest lies inside the word.
bool memoryTargetBoxes(const Expression& lhs, const VariableSymbol& memory, const std::vector<Interval>& dims, ValueRanges& ranges, Lint& l,
                       std::vector<WordBox>& out, std::string& why) {
    std::vector<const Expression*> chain;   // outermost first
    const Expression* cur = &lhs;
    for (;;) {
        if (cur->kind == ExpressionKind::ElementSelect) { chain.push_back(cur); cur = &cur->as<ElementSelectExpression>().value(); }
        else if (cur->kind == ExpressionKind::RangeSelect) { chain.push_back(cur); cur = &cur->as<RangeSelectExpression>().value(); }
        else if (cur->kind == ExpressionKind::MemberAccess) { chain.push_back(cur); cur = &cur->as<MemberAccessExpression>().value(); }
        else if (cur->kind == ExpressionKind::Conversion) cur = &cur->as<ConversionExpression>().operand();
        else break;
    }
    if (cur->kind == ExpressionKind::Concatenation && chain.empty()) {
        for (const auto* operand : cur->as<ConcatenationExpression>().operands())
            if (!memoryTargetBoxes(*operand, memory, dims, ranges, l, out, why)) return false;
        return true;
    }
    if (cur->kind != ExpressionKind::NamedValue) {
        struct Mentions : ASTVisitor<Mentions, VisitFlags::AllGood> {
            const Symbol* memory; bool found = false;
            void handle(const NamedValueExpression& e) { if (&e.symbol == memory) found = true; }
        } mentions;
        mentions.memory = &memory;
        cur->visit(mentions);
        if (!mentions.found) return true;
        why = "an assignment target form the scan does not decompose";
        return false;
    }
    if (&cur->as<NamedValueExpression>().symbol != &memory) return true;
    ReachEnv env(ranges, l);
    // A run-time variable that more than one select of the write reads (or one select reads twice) takes one value for all of them: the
    // reach is the union over the joint values of those variables, enumerated when their joint space is small. Otherwise the independent
    // product of the selects is only a superset: the write's reach is approximate.
    struct Occurrences : ASTVisitor<Occurrences, VisitFlags::AllGood> {
        std::map<const ValueSymbol*, std::pair<const NamedValueExpression*, int>> seen;
        void handle(const NamedValueExpression& e) {
            const auto k = e.symbol.kind;
            if (k == SymbolKind::Parameter || k == SymbolKind::EnumValue || k == SymbolKind::Specparam) return;
            auto& entry = seen[&e.symbol];
            if (!entry.first) entry.first = &e;
            ++entry.second;
        }
    };
    std::map<const ValueSymbol*, std::pair<const NamedValueExpression*, int>> seen;
    std::map<const Expression*, std::set<const ValueSymbol*>> readBy;
    for (const Expression* step : chain) {
        Occurrences o;
        if (step->kind == ExpressionKind::ElementSelect) {
            const auto& select = step->as<ElementSelectExpression>().selector();
            if (!constantInt(l, select)) select.visit(o);
        } else if (step->kind == ExpressionKind::RangeSelect) {
            const auto& select = step->as<RangeSelectExpression>();
            if (!constantInt(l, select.left())) select.left().visit(o);
        }
        for (const auto& [sym, entry] : o.seen) {
            auto& total = seen[sym];
            if (!total.first) total.first = entry.first;
            total.second += entry.second;
            readBy[step].insert(sym);
        }
    }
    std::vector<const ValueSymbol*> vars;
    for (const auto& [sym, entry] : seen)
        if (entry.second > 1) vars.push_back(sym);
    if (vars.empty()) return placeWrite(chain, memory, dims, env, out, why);
    for (const auto& [step, syms] : readBy)
        for (const auto* sym : vars)
            if (syms.count(sym)) env.bound.insert(step);
    std::vector<Interval> spans;
    Wide joint = 1;
    bool enumerable = true;
    for (const auto* sym : vars) {
        const auto range = sym->getType().isIntegral() && sym->getType().getBitWidth() <= 62 ? ranges.of(*seen[sym].first) : std::nullopt;
        if (!range || range->lo > range->hi) { enumerable = false; break; }
        spans.push_back(*range);
        joint *= range->hi - range->lo + 1;
        if (joint > Wide(kMaxStarts)) { enumerable = false; break; }
    }
    if (!enumerable) {
        env.bound.clear();
        env.approx = true;
        return placeWrite(chain, memory, dims, env, out, why);
    }
    for (const auto* sym : vars) env.locals.push_back(env.ec.createLocal(sym));
    std::set<std::vector<Wide>> done;
    for (Wide k = 0; k < joint; ++k) {
        auto rest = k;
        for (size_t i = 0; i < vars.size(); ++i) {
            const Wide size = spans[i].hi - spans[i].lo + 1;
            const auto& type = vars[i]->getType();
            *env.locals[i] = SVInt(type.getBitWidth(), uint64_t(int64_t(spans[i].lo + rest % size)), type.isSigned());
            rest /= size;
        }
        std::vector<WordBox> boxes;
        if (!placeWrite(chain, memory, dims, env, boxes, why)) return false;
        for (auto& box : boxes) {
            std::vector<Wide> key;
            for (const auto& d : box.dims) { key.push_back(d.lo); key.push_back(d.hi); }
            key.push_back(-1);
            for (const auto& b : box.bits) { key.push_back(b.lo); key.push_back(b.hi); }
            key.push_back(box.approx);
            if (done.insert(std::move(key)).second) out.push_back(std::move(box));
        }
    }
    return true;
}

// The parts the bits [lo, hi] of the memory (slang's driver bounds: the elements of an unpacked array are laid out by selectable width, in
// declaration order from bit 0) cover. When the bits span several elements of a dimension, the later dimensions and the bits are left whole.
WordBox boxFromBits(const Type& memoryType, uint64_t lo, uint64_t hi) {
    WordBox box{memoryDimensions(memoryType), {{0, kEveryBit}}};
    const Type* cur = &memoryType.getCanonicalType();
    for (size_t dim = 0; dim < box.dims.size() && cur->kind == SymbolKind::FixedSizeUnpackedArrayType; ++dim) {
        const auto& array = cur->as<FixedSizeUnpackedArrayType>();
        const uint64_t width = array.elementType.getSelectableWidth();
        if (width == 0) break;
        const auto index = [&](uint64_t offset) -> Wide {
            return array.range.isDescending() ? Wide(array.range.upper()) - Wide(offset) : Wide(array.range.lower()) + Wide(offset);
        };
        const Wide a = index(lo / width), b = index(hi / width);
        box.dims[dim] = {std::min(a, b), std::max(a, b)};
        if (lo / width != hi / width) return box;
        lo %= width;
        hi %= width;
        cur = &array.elementType.getCanonicalType();
        if (dim + 1 == box.dims.size() && cur->isIntegral()) box.bits = {{Wide(lo), Wide(hi)}};
    }
    return box;
}

// The first word (smallest index in each dimension in turn) that two boxes share, where they also touch a common bit. `exact` is false when
// the common bit lies only in a superset (a select the model could not place) of one of them.
struct SharedWord { std::vector<Wide> word; bool exact; };
std::optional<SharedWord> sharedWord(const WordBox& a, const WordBox& b) {
    SharedWord out{{}, !a.approx && !b.approx};
    if (a.dims.size() != b.dims.size()) return std::nullopt;
    for (size_t i = 0; i < a.dims.size(); ++i) {
        const Wide lo = std::max(a.dims[i].lo, b.dims[i].lo), hi = std::min(a.dims[i].hi, b.dims[i].hi);
        if (lo > hi) return std::nullopt;
        out.word.push_back(lo);
    }
    bool common = false;
    for (size_t i = 0, j = 0; i < a.bits.size() && j < b.bits.size() && !common;) {
        if (std::max(a.bits[i].lo, b.bits[j].lo) <= std::min(a.bits[i].hi, b.bits[j].hi)) common = true;
        else if (a.bits[i].hi < b.bits[j].hi) ++i;
        else ++j;
    }
    if (!common) return std::nullopt;
    return out;
}
bool boxHolds(const WordBox& box, const std::vector<Wide>& word) {
    for (size_t i = 0; i < box.dims.size(); ++i)
        if (word[i] < box.dims[i].lo || word[i] > box.dims[i].hi) return false;
    return true;
}

// Classify writes in constant if, case and conditional-expression arms.
// A case slang cannot decide retains the write with uncertain reachability.
enum class Arm { Live, Dead, Undecided };
Arm inConstantDeadArm(const std::vector<const Statement*>& scopes, const slang::SourceLocation at, const Statement* body, Lint& l) {
    bool undecided = false;
    // the scopes list names the `case` but not the item holding the write: the item is the one whose statement spans the write
    const auto spans = [&](const Statement* st) {
        return st && at.buffer() == st->sourceRange.start().buffer() && at.offset() >= st->sourceRange.start().offset() &&
               at.offset() < st->sourceRange.end().offset();
    };
    if (body) {   // a `?:` whose constant condition excludes the arm holding the write
        struct Ternary : ASTVisitor<Ternary, VisitFlags::AllGood> {
            Lint& l; slang::SourceLocation at; bool dead = false;
            Ternary(Lint& lint, slang::SourceLocation a) : l(lint), at(a) {}
            bool within(const Expression& e) const {
                return at.buffer() == e.sourceRange.start().buffer() && at.offset() >= e.sourceRange.start().offset() && at.offset() < e.sourceRange.end().offset();
            }
            void handle(const ConditionalExpression& c) {
                if (c.conditions.size() == 1 && !c.conditions[0].pattern && !callsSubroutine(*c.conditions[0].expr)) {
                    const auto cv = l.evalConst(*c.conditions[0].expr);
                    if (cv.isInteger() && !cv.integer().hasUnknown()) {
                        const bool taken = static_cast<bool>(cv.isTrue());
                        if (within(taken ? c.right() : c.left())) dead = true;
                    }
                }
                visitDefault(c);
            }
        } t(l, at);
        body->visit(t);
        if (t.dead) return Arm::Dead;
    }
    for (auto it = scopes.begin(); it != scopes.end(); ++it) {
        const Statement* arm = it + 1 == scopes.end() ? nullptr : *(it + 1);
        if ((*it)->kind == StatementKind::Conditional) {
            if (!arm) continue;
            const auto& c = (*it)->as<ConditionalStatement>();
            const bool inThen = arm == &c.ifTrue;
            if (c.conditions.size() != 1 || c.conditions[0].pattern || (!inThen && arm != c.ifFalse) || callsSubroutine(*c.conditions[0].expr)) continue;
            const auto cv = l.evalConst(*c.conditions[0].expr);
            if (cv.isInteger() && !cv.integer().hasUnknown() && static_cast<bool>(cv.isTrue()) != inThen) return Arm::Dead;
        } else if ((*it)->kind == StatementKind::Case) {
            const auto& c = (*it)->as<CaseStatement>();
            bool calls = callsSubroutine(c.expr);
            for (const auto& g : c.items)
                for (const auto* e : g.expressions) calls = calls || callsSubroutine(*e);
            if (calls) continue;
            // the arm holding the write: one of the items' statements or the default
            const Statement* holding = nullptr;
            for (const auto& g : c.items)
                if (spans(g.stmt)) { holding = g.stmt; break; }
            if (!holding && spans(c.defaultCase)) holding = c.defaultCase;
            if (!holding) continue;
            // Use slang's case matching semantics, including X/Z wildcards and ranges.
            const auto selector = l.evalConst(c.expr);
            if (!selector.isInteger()) continue;
            ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
            EvalContext ec(ac);
            const auto [taken, decided] = c.getKnownBranch(ec);
            if (!decided) { undecided = true; continue; }
            if (taken != holding) return Arm::Dead;
        }
    }
    return undecided ? Arm::Undecided : Arm::Live;
}

class MemoryRule : public LintRule {
public:
    explicit MemoryRule(bool writers) : LintRule(writers ? "LNT-049" : "LNT-082"), writerRule(writers) {}
private:
    bool writerRule;
    // The value or index depends on constants and the counters of the enclosing constant-bound loops only: it is the same at every time zero.
    struct LoopConstant : ASTVisitor<LoopConstant, VisitFlags::AllGood> {
        const std::vector<const ValueSymbol*>& vars;
        bool ok = true;
        explicit LoopConstant(const std::vector<const ValueSymbol*>& v) : vars(v) {}
        void handle(const NamedValueExpression& e) {
            const auto k = e.symbol.kind;
            if (k == SymbolKind::Parameter || k == SymbolKind::EnumValue || k == SymbolKind::Specparam) return;
            if (std::find(vars.begin(), vars.end(), &e.symbol) == vars.end()) ok = false;
        }
        void handle(const HierarchicalValueExpression&) { ok = false; }
        void handle(const CallExpression& c) {
            static const std::set<std::string_view> pure = {"$clog2", "$signed", "$unsigned", "$bits", "$countones"};
            if (!c.isSystemCall() || !pure.count(c.getSubroutineName())) { ok = false; return; }
            visitDefault(c);
        }
    };
    static bool loopConstant(const Expression& e, Lint& l, const std::vector<const ValueSymbol*>& vars) {
        auto value = l.evalConst(e);
        if (value.isInteger() && !value.integer().hasUnknown()) return true;
        LoopConstant c(vars);
        e.visit(c);
        return c.ok;
    }
    static bool staticInitTarget(const Expression& e, Lint& l, const std::vector<const ValueSymbol*>& vars) {
        if (e.kind == ExpressionKind::NamedValue) return true;
        if (e.kind != ExpressionKind::ElementSelect) return false;
        const auto& select = e.as<ElementSelectExpression>();
        return loopConstant(select.selector(), l, vars) && staticInitTarget(select.value(), l, vars);
    }
    // A time-zero fill of a table: constant assignments, `$readmemh/$readmemb` of a constant file name, and constant-bound `for` loops over them.
    static bool staticInitBody(const Statement& s, Lint& l, std::vector<const ValueSymbol*>& vars) {
        switch (s.kind) {
            case StatementKind::Empty: return true;
            case StatementKind::Block: {
                const auto& b = s.as<BlockStatement>();
                return b.blockKind == StatementBlockKind::Sequential && staticInitBody(b.body, l, vars);
            }
            case StatementKind::List:
                for (auto* child : s.as<StatementList>().list)
                    if (!staticInitBody(*child, l, vars)) return false;
                return true;
            case StatementKind::ForLoop: {
                const auto& loop = s.as<ForLoopStatement>();
                const auto values = constantLoopValues(loop, l);
                if (!values) return false;
                vars.push_back(values->var);
                const bool ok = staticInitBody(loop.body, l, vars);
                vars.pop_back();
                return ok;
            }
            case StatementKind::ExpressionStatement: {
                const auto& e = s.as<ExpressionStatement>().expr;
                if (e.kind == ExpressionKind::Call && e.as<CallExpression>().isSystemCall()) {
                    const auto& call = e.as<CallExpression>();
                    const auto name = call.getSubroutineName();
                    if (name != "$readmemh" && name != "$readmemb") return false;
                    for (const auto* arg : call.arguments())
                        if (!arg->type->isUnpackedArray() && !loopConstant(*arg, l, vars) && !l.evalConst(*arg).isString()) return false;
                    return true;
                }
                if (e.kind != ExpressionKind::Assignment) return false;
                const auto& a = e.as<AssignmentExpression>();
                return !a.isCompound() && !a.isNonBlocking() && staticInitTarget(a.left(), l, vars) && loopConstant(a.right(), l, vars);
            }
            case StatementKind::VariableDeclaration: return true;   // a local; what reads it must pass loopConstant
            default: return false;
        }
    }
    static bool initializationOnly(const Symbol* s, Lint& l) {
        const auto* p = s ? s->as_if<ProceduralBlockSymbol>() : nullptr;
        if (!p || p->procedureKind != ProceduralBlockKind::Initial) return false;
        std::vector<const ValueSymbol*> vars;
        return staticInitBody(p->getBody(), l, vars);
    }
    // How each memory is written, from the assignments themselves: by which processes, with which kinds, and whether every index is fixed at elaboration (a
    // constant, or the counter of a constant-bound loop). A write the scan cannot see through (a subroutine, a system task output, a port) is `unstatic`.
    struct WriteInfo {
        std::set<const Symbol*> processes;
        bool blocking = false, comb = false, unstatic = false;
        // Writes at a run-time address in level-sensitive code (always_latch, always_comb, always @*) that does not first write the whole array:
        // state, written while its enable is high through every address the decoder passes (LNT-049).
        std::vector<std::pair<slang::SourceLocation, const Symbol*>> levelWrites;
    };
    static bool isMemory(const Symbol* s) {
        const auto* v = s ? s->as_if<VariableSymbol>() : nullptr;
        return v && isModuleLevel(*v) && v->getType().isUnpackedArray();
    }
    struct MemoryWrites : HardwareVisitor<MemoryWrites, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Symbol*, WriteInfo>& info;
        const ProceduralBlockSymbol* proc = nullptr;
        bool skip = false;
        unsigned inSubroutine = 0;
        std::vector<const ValueSymbol*> loops;
        MemoryWrites(Lint& lint, std::map<const Symbol*, WriteInfo>& out) : l(lint), info(out) {}
        bool fixedIndex(const Expression& e) {
            auto v = l.evalConst(e);
            if (v.isInteger() && !v.integer().hasUnknown()) return true;
            return loopConstant(e, l, loops);
        }
        bool fixedTarget(const Expression& e) {
            switch (e.kind) {
                case ExpressionKind::NamedValue: return true;
                case ExpressionKind::ElementSelect: return fixedIndex(e.as<ElementSelectExpression>().selector()) && fixedTarget(e.as<ElementSelectExpression>().value());
                case ExpressionKind::RangeSelect: {
                    const auto& r = e.as<RangeSelectExpression>();
                    return fixedIndex(r.left()) && fixedIndex(r.right()) && fixedTarget(r.value());
                }
                case ExpressionKind::MemberAccess: return fixedTarget(e.as<MemberAccessExpression>().value());
                case ExpressionKind::Conversion: return fixedTarget(e.as<ConversionExpression>().operand());
                default: return false;
            }
        }
        std::set<std::pair<const ProceduralBlockSymbol*, const Symbol*>> wholeWritten;   // (process, memory) written whole earlier in the procedure
        void note(const Symbol* memory, bool blocking, bool comb, bool fixed, const AssignmentExpression* at = nullptr) {
            auto& i = info[memory];
            // the address must come from design state (a port or a variable of the module): a counter or a block-local offset is no run-time address
            bool designAddress = false;
            if (at) {
                struct Names : ASTVisitor<Names, VisitFlags::AllGood> {
                    const Symbol* memory; bool found = false;
                    void handle(const NamedValueExpression& e) {
                        if (&e.symbol != memory && e.symbol.kind != SymbolKind::Parameter && e.symbol.kind != SymbolKind::EnumValue)
                            if (const auto* v = e.symbol.as_if<ValueSymbol>(); v && isModuleLevel(*v)) found = true;
                    }
                } names;
                names.memory = memory;
                at->left().visit(names);
                designAddress = names.found;
            }
            if (at && proc && comb && !fixed && designAddress && inSubroutine == 0 && proc->procedureKind != ProceduralBlockKind::Initial &&
                proc->procedureKind != ProceduralBlockKind::Final && !wholeWritten.count({proc, memory}))
                i.levelWrites.push_back({at->sourceRange.start(), proc});
            if (at && proc && at->left().kind == ExpressionKind::NamedValue) wholeWritten.insert({proc, memory});
            i.processes.insert(proc);
            i.blocking = i.blocking || blocking;
            i.comb = i.comb || comb;
            // an initial or final write is not hardware the clock drives (a delayed or computed fill): not a register array
            i.unstatic = i.unstatic || !fixed || inSubroutine > 0 ||
                         (proc && (proc->procedureKind == ProceduralBlockKind::Initial || proc->procedureKind == ProceduralBlockKind::Final));
        }
        void handle(const ProceduralBlockSymbol& p) {
            auto* oldProc = proc;
            const bool oldSkip = skip;
            proc = &p;
            skip = initializationOnly(&p, l);
            visitDefault(p);
            proc = oldProc;
            skip = oldSkip;
        }
        void handle(const SubroutineSymbol& sub) {
            ++inSubroutine;
            visitDefault(sub);
            --inSubroutine;
        }
        void handle(const ForLoopStatement& loop) {
            const auto values = constantLoopValues(loop, l);
            if (values) loops.push_back(values->var);
            visitDefault(loop);
            if (values) loops.pop_back();
        }
        void handle(const AssignmentExpression& a) {
            if (!skip)
                for (const auto* sym : lvalueSymbols(a.left()))
                    if (isMemory(sym)) note(sym, !a.isNonBlocking(), proc == nullptr || !isClocked(*proc), fixedTarget(a.left()), &a);
            visitDefault(a);
        }
        void handle(const CallExpression& call) {
            if (!skip && call.isSystemCall()) {
                const auto name = call.getSubroutineName();
                if (name.rfind("$readmem", 0) == 0 || name.rfind("$fscanf", 0) == 0 || name.rfind("$sscanf", 0) == 0 || name.rfind("$fread", 0) == 0)
                    for (const auto* arg : call.arguments())
                        for (const auto* sym : lvalueSymbols(*arg))
                            if (isMemory(sym)) note(sym, true, false, false);
            }
            visitDefault(call);
        }
    };
    struct MemoryRead {
        const Expression* ref = nullptr;   // null for a read found only inside a called subroutine body
        SourceLocation loc;
        SourceLocation raw;                // the unexpanded location, as the shared read inventory keeps it
        const Symbol* process = nullptr;
        bool exact = false;                // a static word range, in the bit coordinates of slang's driver bounds
        uint64_t lo = 0, hi = 0;
    };
    static bool containsMemory(const Type& t) {
        const auto& type = t.getCanonicalType();
        if (type.isUnpackedArray()) return true;
        if (type.isStruct() || type.isUnion())
            for (const auto& field : type.as<Scope>().membersOfType<FieldSymbol>())
                if (containsMemory(field.getType())) return true;
        return false;
    }
    struct MemoryReads : HardwareVisitor<MemoryReads, VisitFlags::AllCanonical> {
        const ProceduralBlockSymbol* process = nullptr;
        std::map<const Symbol*, std::vector<MemoryRead>>& reads;
        explicit MemoryReads(std::map<const Symbol*, std::vector<MemoryRead>>& out) : reads(out) {}
        template <typename N> void collect(const N& node) {
            Reads r;
            node.visit(r);
            for (auto* ref : r.refs)
                if (auto* sym = ref->getSymbolReference(); sym && sym->as_if<VariableSymbol>() &&
                    isModuleLevel(sym->as<VariableSymbol>()) && containsMemory(sym->as<VariableSymbol>().getType()))
                    reads[sym].push_back({ref, {}, {}, process});
        }
        void handle(const ProceduralBlockSymbol& p) {
            auto* old = process;
            process = &p;
            if (p.procedureKind != ProceduralBlockKind::Initial && p.procedureKind != ProceduralBlockKind::Final)
                collect(p.getBody());
            visitDefault(p);
            process = old;
        }
        void handle(const ContinuousAssignSymbol& ca) { collect(ca.getAssignment()); visitDefault(ca); }
    };
public:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        bool writerRule;
        const std::map<const Symbol*, std::set<const Symbol*>>& nonConstant;
        const std::map<const Symbol*, std::vector<MemoryRead>>& memoryReads;
        const std::set<const Symbol*>& registerArrays;   // written only at indices fixed at elaboration: registers, not a memory
        const std::map<const Symbol*, ClockKey>& definedPort;   // one clocked process writes it with nonblocking assignments: read-first, defined in simulation, on this clock
        const std::map<const Symbol*, WriteInfo>& writeInfo;
        V(Lint& lint, bool writers, const std::map<const Symbol*, std::set<const Symbol*>>& nc,
          const std::map<const Symbol*, std::vector<MemoryRead>>& reads, const std::set<const Symbol*>& regs, const std::map<const Symbol*, ClockKey>& ports,
          const std::map<const Symbol*, WriteInfo>& infos)
            : l(lint), writerRule(writers), nonConstant(nc), memoryReads(reads), registerArrays(regs), definedPort(ports), writeInfo(infos) {}
        // The defined read-first behaviour covers a read the same clock registers and one that is combinational; a read clocked by
        // another clock meets the write asynchronously, and what it gets is the memory's, not the RTL's.
        bool readIsDefined(const Symbol* memory, const MemoryRead& read) const {
            auto it = definedPort.find(memory);
            if (it == definedPort.end()) return false;
            if (!read.process) return true;
            const auto* p = read.process->as_if<ProceduralBlockSymbol>();
            if (!p || !isClocked(*p)) return true;
            return clockKeyOf(*p, model::getModel(l.context().design)) == it->second;
        }
        static bool isMem(const Symbol* s) { auto* v = s ? s->as_if<VariableSymbol>() : nullptr; return v && isModuleLevel(*v) && v->getType().isUnpackedArray(); }
        template <typename N> void asyncRead(const N& e) {
            if (writerRule) return;
            Reads r;
            e.visit(r);
            for (auto* ref : r.refs)
                if (auto* sym = ref->getSymbolReference(); isMem(sym) && !registerArrays.count(sym) && l.isUser(ref->sourceRange.start())) {
                    l.count();
                    l.report(ref->sourceRange.start(), "memory '" + std::string(sym->name) + "' is read asynchronously (combinational read): it maps to distributed/LUT RAM, not a synchronous block RAM",
                             {.severity = Severity::Info, .confidence = Confidence::Informational, .nextStep = "Register the read data if a block RAM is intended.", .signals = {std::string(sym->name)}});
                }
        }
        void handle(const ContinuousAssignSymbol& ca) { asyncRead(ca.getAssignment()); visitDefault(ca); }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                if (!isClocked(p) && p.procedureKind != ProceduralBlockKind::Initial && p.procedureKind != ProceduralBlockKind::Final) asyncRead(p.getBody().kind == StatementKind::Timed ? p.getBody().as<TimedStatement>().stmt : p.getBody());
            }
            visitDefault(p);
        }
        // The signal a clock is a copy or an inversion of (`wire c = clk;`, `assign c = ~clk;`): processes on such signals are on one clock.
        const Symbol* clockRoot(const Symbol* s) const {
            const auto* an = l.context().design.analysis.get();
            for (int depth = 0; depth < 16 && s && an; ++depth) {
                const auto* value = s->as_if<ValueSymbol>();
                if (!value) return s;
                const Symbol* only = nullptr;
                unsigned n = 0;
                for (const auto* d : an->getDrivers(*value)) {
                    if (d->isUnidirectionalPort()) continue;
                    ++n;
                    only = d->containingSymbol;
                }
                // `assign c = clk;`, or the initializer of the net, `wire c = clk;`
                const Expression* rhs = nullptr;
                if (n == 0 && value->kind == SymbolKind::Net) {
                    rhs = value->getInitializer();
                } else if (n == 1 && only->kind == SymbolKind::ContinuousAssign) {
                    const auto& e = only->as<ContinuousAssignSymbol>().getAssignment();
                    if (e.kind != ExpressionKind::Assignment) return s;
                    const auto& a = e.as<AssignmentExpression>();
                    if (a.left().kind != ExpressionKind::NamedValue || &a.left().as<NamedValueExpression>().symbol != s) return s;
                    rhs = &a.right();
                }
                if (!rhs) return s;
                const Expression* r = &rhs->unwrapImplicitConversions();
                while (r->kind == ExpressionKind::UnaryOp && (r->as<UnaryExpression>().op == UnaryOperator::BitwiseNot ||
                                                              r->as<UnaryExpression>().op == UnaryOperator::LogicalNot))
                    r = &r->as<UnaryExpression>().operand().unwrapImplicitConversions();
                if (r->kind != ExpressionKind::NamedValue) return s;
                s = &r->as<NamedValueExpression>().symbol;
            }
            return s;
        }
        // The clock signals (roots, edges ignored) of a clocked process; empty when its events cannot be read as clocks.
        std::set<const Symbol*> clockSignals(const Symbol* process) const {
            std::set<const Symbol*> out;
            const auto* p = process ? process->as_if<ProceduralBlockSymbol>() : nullptr;
            if (!p || !isClocked(*p)) return out;
            for (const auto& [sym, edge] : clockKeyOf(*p, model::getModel(l.context().design)).events) out.insert(clockRoot(sym));
            return out;
        }
        // Whether the words a write can reach (by the value ranges of its indices) share no word with the static word range of `read`.
        bool writerMissesRead(const VariableSymbol& v, const model::ProceduralWrite& write, const MemoryRead& read) {
            ValueRanges ranges(l);
            std::vector<const ForLoopStatement*> entered;
            for (const auto* scope : write.scopes)
                if (scope->kind == StatementKind::ForLoop && ranges.enter(scope->as<ForLoopStatement>())) entered.push_back(&scope->as<ForLoopStatement>());
            std::vector<WordBox> boxes;
            std::string why;
            const bool ok = memoryTargetBoxes(*write.targetExpr, v, memoryDimensions(v.getType()), ranges, l, boxes, why);
            for (auto it = entered.rbegin(); it != entered.rend(); ++it) ranges.leave(**it);
            if (!ok) return false;
            const WordBox target = boxFromBits(v.getType(), read.lo, read.hi);
            return std::none_of(boxes.begin(), boxes.end(), [&](const WordBox& box) { return sharedWord(box, target).has_value(); });
        }
        // A word written on one clock and read on another, unrelated one: what the reader sees depends on the phase between the clocks, and the
        // memory the array becomes (or the registers) decides the rest. Disclosed once per array and pair of clock sets, at a write and a read.
        void crossClockReads(const VariableSymbol& v, const std::vector<MemoryRead>& reads,
                             const std::set<const Symbol*>& dynamic, bool fullyAttributed, bool readDisclosed) {
            using Clocks = std::set<const Symbol*>;
            std::set<std::pair<Clocks, Clocks>> done;
            const auto& writes = model::getModel(l.context().design).proceduralWrites;
            for (const auto& read : reads) {
                if (!read.loc.valid() || !read.process) continue;
                const Clocks readClocks = clockSignals(read.process);
                if (readClocks.empty()) continue;
                for (const auto& write : writes) {
                    if (write.called || write.target != &v || !write.process || !isClocked(*write.process)) continue;
                    Clocks writeClocks = clockSignals(write.process);
                    if (writeClocks.empty() || writeClocks == readClocks) continue;
                    // a read of a static word range that this writer cannot reach is no overlap
                    const bool writerStatic = fullyAttributed && !dynamic.count(write.process) && write.exactTarget;
                    if (writerStatic && read.exact && !(write.lo <= read.hi && read.lo <= write.hi)) continue;
                    // a run-time writer reaches the words its index's value range names: a read of a static word outside them is no overlap
                    if (!writerStatic && read.exact && write.targetExpr && writerMissesRead(v, write, read)) continue;
                    if (!done.insert({writeClocks, readClocks}).second) continue;
                    const std::string name(v.name);
                    const std::string msg = "memory '" + name + "' is written on one clock and read on another, unrelated one: the word the read sees depends on the phase between the clocks, so the read semantics are not decided";
                    auto options = [&](const SourceLocation& other, const char* what) {
                        return LintOpts{.severity = Severity::Warning, .confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                                        .nextStep = "Cross the data between the clocks through a synchroniser or a handshake, or use a dual-clock memory whose behavior is stated and verified.",
                                        .hierarchy = v.getHierarchicalPath(), .signals = {name},
                                        .evidence = {{"memory", name, {}}, {what, "the other side of the cross-clock access of this array", other}},
                                        .obligation = "cross-clock-array-read"};
                    };
                    l.reportAt(write.loc, msg, options(read.loc, "memory_read"));
                    if (!readDisclosed) {
                        if (read.ref) l.report(read.ref->sourceRange.start(), msg, options(write.loc, "memory_write"));
                        else l.reportAt(read.loc, msg, options(write.loc, "memory_write"));
                    }
                }
            }
        }
        // LNT-049: two processes that can write the same word of the memory. The word is the element of the unpacked dimensions; a packed part-
        // or bit-select inside it never separates two writers, and an index narrows the words only through its value range (a constant, a
        // generate genvar, a constant-bound loop counter, or the range the value proofs give a run-time index). A write whose words cannot
        // be placed, or a writer the scan does not follow, is Unresolved while another process writes the memory.
        void reportSharedWords(const VariableSymbol& v, const std::string& name) {
            struct Site { SourceLocation at; std::vector<WordBox> boxes; bool continuous = false; };
            std::map<const Symbol*, std::vector<Site>> sites;
            std::vector<std::pair<SourceLocation, std::string>> unexamined;
            std::set<const Symbol*> unexaminedProcesses, calledProcesses, deadExcluded, undecidedArm, storesNothing;   // deadExcluded: processes with a write in a dead arm; undecidedArm: in an arm a constant selector leaves undecided; storesNothing: a write always outside the memory
            const auto& model = model::getModel(l.context().design);
            const auto dims = memoryDimensions(v.getType());
            ValueRanges ranges(l);
            auto hardware = [&](const Symbol* process) {
                const auto* p = process ? process->as_if<ProceduralBlockSymbol>() : nullptr;
                return !p || (p->procedureKind != ProceduralBlockKind::Initial && p->procedureKind != ProceduralBlockKind::Final);
            };
            for (const auto& write : model.proceduralWrites) {
                if (write.target != &v && !(write.called && !write.target)) continue;
                if (write.called) { if (write.process) calledProcesses.insert(write.process); continue; }
                if (!write.process || !hardware(write.process)) continue;
                // a write in an arm an elaboration-constant `if` condition or `case` selector excludes never runs
                const Arm arm = write.targetExpr ? inConstantDeadArm(write.scopes, write.targetExpr->sourceRange.start(), &write.process->getBody(), l) : Arm::Live;
                if (arm == Arm::Dead) { deadExcluded.insert(write.process); continue; }
                if (arm == Arm::Undecided) undecidedArm.insert(write.process);
                std::vector<WordBox> boxes;
                std::string why;
                bool ok = write.targetExpr != nullptr;
                if (!ok) why = "a write without a target expression";
                else {
                    std::vector<const ForLoopStatement*> entered;
                    for (const auto* scope : write.scopes)
                        if (scope->kind == StatementKind::ForLoop && ranges.enter(scope->as<ForLoopStatement>())) entered.push_back(&scope->as<ForLoopStatement>());
                    ok = memoryTargetBoxes(*write.targetExpr, v, dims, ranges, l, boxes, why);
                    for (auto it = entered.rbegin(); it != entered.rend(); ++it) ranges.leave(**it);
                }
                if (!ok) { unexamined.push_back({write.loc, why}); unexaminedProcesses.insert(write.process); continue; }
                if (!boxes.empty()) sites[write.process].push_back({write.loc, std::move(boxes), false});
                else storesNothing.insert(write.process);
            }
            std::set<const Symbol*> seenContinuous;
            for (auto* d : l.context().design.analysis->getDrivers(v)) {
                if (d->isInputPort()) continue;
                const Symbol* process = d->containingSymbol;
                if (process->kind == SymbolKind::Subroutine || !hardware(process)) continue;
                if (process->kind == SymbolKind::ContinuousAssign) {
                    if (!seenContinuous.insert(process).second) continue;
                    const auto at = l.loc(d->getSourceRange().start());
                    const auto& e = process->as<ContinuousAssignSymbol>().getAssignment();
                    std::vector<WordBox> boxes;
                    std::string why;
                    if (e.kind != ExpressionKind::Assignment || !memoryTargetBoxes(e.as<AssignmentExpression>().left(), v, dims, ranges, l, boxes, why)) {
                        unexamined.push_back({at, why.empty() ? "a continuous assignment the scan does not decompose" : why});
                        unexaminedProcesses.insert(process);
                    } else if (!boxes.empty()) {
                        sites[process].push_back({at, std::move(boxes), true});
                    }
                } else if (process->kind != SymbolKind::ProceduralBlock && !d->path.empty() && d->path.isFullyStatic()) {
                    // an instance output connected to an element: the bounds slang gives place the word exactly
                    const auto [lo, hi] = d->getBounds();
                    sites[process].push_back({l.loc(d->getSourceRange().start()), {boxFromBits(v.getType(), lo, hi)}, false});
                } else if (!sites.count(process) && !unexaminedProcesses.count(process) && !calledProcesses.count(process) && !deadExcluded.count(process)) {
                    unexamined.push_back({l.loc(d->getSourceRange().start()), "a write through a port connection, a call argument or another construct the scan does not follow"});
                    unexaminedProcesses.insert(process);
                }
            }
            if (auto it = nonConstant.find(&v); it != nonConstant.end())
                for (const auto* process : it->second)
                    if (hardware(process) && !sites.count(process) && !unexaminedProcesses.count(process) && !calledProcesses.count(process) && !storesNothing.count(process)) {
                        unexamined.push_back({l.loc(process->location), "a write through a run-time index the model does not follow"});
                        unexaminedProcesses.insert(process);
                    }
            std::optional<std::vector<Wide>> shared, approxShared;   // approxShared: a common bit only in a superset of the bits a write reaches
            for (auto p = sites.begin(); p != sites.end(); ++p)
                for (auto q = std::next(p); q != sites.end(); ++q)
                    for (const auto& a : p->second)
                        for (const auto& b : q->second)
                            for (const auto& x : a.boxes)
                                for (const auto& y : b.boxes)
                                    if (auto hit = sharedWord(x, y)) {
                                        auto& slot = hit->exact ? shared : approxShared;
                                        if (!slot || hit->word < *slot) slot = std::move(hit->word);
                                    }
            std::set<const Symbol*> everyWriter = unexaminedProcesses;
            for (const auto& [process, list] : sites) everyWriter.insert(process);
            if (shared) {
                std::string at;
                for (const auto k : *shared) at += "[" + toString(k) + "]";
                std::vector<Evidence> evidence;
                std::set<const Symbol*> holders;
                for (const auto& [process, list] : sites)
                    for (const auto& site : list)
                        if (std::any_of(site.boxes.begin(), site.boxes.end(), [&](const WordBox& box) { return boxHolds(box, *shared); })) {
                            holders.insert(process);
                            evidence.push_back({"writer", std::string(site.continuous ? "continuous write" : "write") + " of memory '" + name + "' that can reach word " + at, site.at});
                        }
                const bool onUndecided = std::any_of(holders.begin(), holders.end(), [&](const Symbol* process) { return undecidedArm.count(process) != 0; });
                if (onUndecided)
                    l.report(v, "Unresolved: memory '" + name + "' has " + std::to_string(holders.size()) + " write processes that can write the same word " + at +
                                    ", but a write sits in a `case` arm that its constant selector does not decide, so whether it runs is not known",
                             {.confidence = Confidence::Unresolved, .signals = {name}, .evidence = std::move(evidence), .identityKey = "multiple-writers-undecided-arm"});
                else
                    l.report(v, "memory '" + name + "' has " + std::to_string(holders.size()) + " write processes that can write the same word " + at,
                             {.nextStep = "Give each word one write process, or confirm the target memory has a write port for each; different clocks make it a dual-clock memory (see CDC-035).",
                              .signals = {name}, .evidence = std::move(evidence), .identityKey = "multiple-writers"});
            } else if (approxShared) {
                std::string at;
                for (const auto k : *approxShared) at += "[" + toString(k) + "]";
                l.report(v, "Unresolved: memory '" + name + "' has write processes whose writes inside word " + at +
                                " are only approximated (a select inside the word is not placed) and overlap in that approximation, so whether two of them write one bit is not known",
                         {.confidence = Confidence::Unresolved, .signals = {name}, .identityKey = "multiple-writers-approximate-bits"});
            } else if (!unexamined.empty() && everyWriter.size() > 1) {
                l.report(v, "Unresolved: memory '" + name + "' is written by more than one process, and a write cannot be examined (" + unexamined.front().second +
                                "), so whether two of them can write the same word is not known",
                         {.confidence = Confidence::Unresolved, .signals = {name}, .identityKey = "multiple-writers-unexamined"});
            }
        }
        std::set<const Symbol*> hasWriter;     // memories (and aggregates holding one) with a write process
        void handle(const VariableSymbol& v) {
            if (!writerRule && l.isUser(v.location) && isModuleLevel(v) && !v.getType().isUnpackedArray() &&
                containsMemory(v.getType()) && l.context().design.analysis) {
                // The words of a memory inside an aggregate are not attributed: every read of the aggregate while it has a
                // writer keeps an unresolved collision obligation.
                bool written = nonConstant.count(&v) != 0;
                for (auto* d : l.context().design.analysis->getDrivers(v))
                    if (!d->isInputPort() && !initializationOnly(d->containingSymbol, l)) written = true;
                for (const auto& write : model::getModel(l.context().design).proceduralWrites)
                    if (write.called && (write.target == &v || (!write.target && (!write.mayWriteKnown ||
                            std::find(write.mayWrite.begin(), write.mayWrite.end(), &v) != write.mayWrite.end()))))
                        written = true;
                if (written) hasWriter.insert(&v);
                auto ri = memoryReads.find(&v);
                if (written && ri != memoryReads.end())
                    for (const auto& read : ri->second) {
                        if (!read.loc.valid()) continue;
                        l.count();
                        const std::string name(v.name);
                        const std::string msg = "'" + name + "' holds a memory inside an aggregate and is read while it has a write process; its words are not attributed, so read-during-write collision is not examined";
                        LintOpts o{.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                                   .proof = ProofState::NotAttempted,
                                   .nextStep = "Move the memory to its own array so its read and write addresses can be examined, or state and verify the intended RAM collision behavior.",
                                   .hierarchy = v.getHierarchicalPath(), .signals = {name}};
                        if (read.ref) l.report(read.ref->sourceRange.start(), msg, std::move(o));
                        else l.reportAt(read.loc, msg, std::move(o));
                    }
            }
            if (writerRule && l.isUser(v.location) && isModuleLevel(v) && !v.getType().isUnpackedArray() && containsMemory(v.getType())) {
                std::set<const ProceduralBlockSymbol*> sites;
                for (const auto& write : model::getModel(l.context().design).proceduralWrites)
                    if (write.target == &v && sites.insert(write.process).second)
                        l.reportAt(write.loc, "Unresolved: '" + std::string(v.name) + "' holds a memory inside an aggregate; its words are not attributed, so writers of the memory are not compared",
                                   {.confidence = Confidence::Unresolved, .signals = {std::string(v.name)}});
            }
            std::set<std::tuple<std::string, uint32_t, uint32_t>> calledSites;
            if (writerRule && l.isUser(v.location) && isModuleLevel(v) && v.getType().isUnpackedArray())
                for (const auto& write : model::getModel(l.context().design).proceduralWrites)
                    if (write.called && (write.target == &v || (!write.target && (!write.mayWriteKnown ||
                            std::find(write.mayWrite.begin(), write.mayWrite.end(), &v) != write.mayWrite.end()))) &&
                        calledSites.insert({write.loc.file, write.loc.line, write.loc.column}).second)
                        l.reportAt(write.loc, "Unresolved: " + write.reason + " for memory '" + std::string(v.name) + "'",
                                   {.confidence = Confidence::Unresolved, .signals = {std::string(v.name)}});
            if (l.isUser(v.location) && isModuleLevel(v) && v.getType().isUnpackedArray() && l.context().design.analysis) {
                l.count();
                std::set<const Symbol*> drivers;      // processes with a driver for the memory
                std::set<const Symbol*> dynamic;      // writers that may reach any word
                std::vector<std::pair<std::pair<uint64_t, uint64_t>, const Symbol*>> words;
                bool comb = false;
                for (auto* d : l.context().design.analysis->getDrivers(v)) {
                    if (d->isInputPort()) continue;
                    // A subroutine definition is not an independent process. Its writes are
                    // attributed below to each caller by the shared scheduling inventory.
                    if (writerRule && d->containingSymbol->kind == SymbolKind::Subroutine) continue;
                    if (!writerRule && initializationOnly(d->containingSymbol, l)) continue;
                    drivers.insert(d->containingSymbol);
                    comb = comb || d->kind == analysis::DriverKind::Continuous || d->source == analysis::DriverSource::AlwaysComb ||
                           d->source == analysis::DriverSource::AlwaysLatch ||
                           (d->containingSymbol->kind == SymbolKind::ProceduralBlock &&
                            !isClocked(d->containingSymbol->as<ProceduralBlockSymbol>()) &&
                            d->containingSymbol->as<ProceduralBlockSymbol>().procedureKind == ProceduralBlockKind::Always);
                    if (d->path.empty() || !d->path.isFullyStatic()) dynamic.insert(d->containingSymbol);
                    else words.push_back({d->getBounds(), d->containingSymbol});
                }
                if (!writerRule) {
                    // A write whose body or target the shared inventory cannot examine may reach any word.
                    bool opaqueWriter = false;
                    for (const auto& write : model::getModel(l.context().design).proceduralWrites)
                        if (write.called && (write.target == &v || (!write.target && (!write.mayWriteKnown ||
                                std::find(write.mayWrite.begin(), write.mayWrite.end(), &v) != write.mayWrite.end()))))
                            opaqueWriter = true;
                    auto ri = memoryReads.find(&v);
                    const bool written = !drivers.empty() || nonConstant.count(&v) || opaqueWriter;
                    if (written) hasWriter.insert(&v);
                    if (ri != memoryReads.end() && written && !registerArrays.count(&v)) {
                        // Every writer reaches a known word range: a read of a static range no writer overlaps cannot collide.
                        const bool writersStatic = dynamic.empty() && !nonConstant.count(&v) && !opaqueWriter;
                        for (const auto& read : ri->second) {
                            if (!read.loc.valid()) continue;
                            if (readIsDefined(&v, read)) continue;
                            if (writersStatic && read.exact &&
                                std::none_of(words.begin(), words.end(), [&](const auto& w) {
                                    return w.first.first <= read.hi && read.lo <= w.first.second; }))
                                continue;
                            l.count();
                            const std::string name(v.name);
                            const std::string msg = "memory '" + name + "' is read while the memory has a write process; read-during-write collision and target behavior are not proven";
                            LintOpts o{.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                                       .proof = ProofState::NotAttempted,
                                       .nextStep = "Prove the read and write addresses cannot collide, or state and verify the intended RAM collision behavior.",
                                       .hierarchy = v.getHierarchicalPath(), .signals = {name}, .evidence = {{"memory", name, {}},
                                           {"memory_read", "array element read; index relationship is not proven", read.loc},
                                           {"proof_obligation", "read/write address collision and target read-during-write behavior are not modelled", {}}},
                                       .obligation = "memory-read-during-write"};
                            if (read.ref) l.report(read.ref->sourceRange.start(), msg, std::move(o));
                            else l.reportAt(read.loc, msg, std::move(o));
                        }
                    }
                    if (ri != memoryReads.end() && written) {
                        // a read the obligation above already reports is not reported twice
                        crossClockReads(v, ri->second, dynamic, dynamic.empty() && !nonConstant.count(&v) && !opaqueWriter, registerArrays.count(&v) == 0);
                    }
                }
                for (const auto& write : model::getModel(l.context().design).proceduralWrites) {
                    if (write.called || write.target != &v || !writerRule) continue;
                    drivers.insert(write.process);
                    comb = comb || (!isClocked(*write.process) &&
                        write.process->procedureKind != ProceduralBlockKind::Initial &&
                        write.process->procedureKind != ProceduralBlockKind::Final);
                }
                std::string name(v.name);
                if (writerRule) reportSharedWords(v, name);
                const auto* infoOf = [&]() -> const WriteInfo* { auto it = writeInfo.find(&v); return it == writeInfo.end() ? nullptr : &it->second; }();
                const bool levelWritten = infoOf && !infoOf->levelWrites.empty();
                if (writerRule && levelWritten)
                    for (const auto& [at, process] : infoOf->levelWrites) {
                        const auto kind = process->as<ProceduralBlockSymbol>().procedureKind;
                        l.report(at, "memory '" + name + "' is written at a run-time address by level-sensitive logic (" +
                                         (kind == ProceduralBlockKind::AlwaysLatch ? "always_latch" : kind == ProceduralBlockKind::AlwaysComb ? "always_comb" : "a level-sensitive always block") +
                                         "): it infers latches, and while the enable is high every address the decoder passes through is written, which simulation does not show",
                                 {.severity = Severity::Warning, .confidence = Confidence::Established,
                                  .nextStep = "Write the memory from a clocked process, or assign every word first if a combinational table is meant.",
                                  .signals = {name}});
                    }
                if (writerRule && comb && !levelWritten)
                    l.report(v, "memory '" + name + "' is written by combinational logic: it implements combinational wiring or a table, not a RAM",
                             {.severity = Severity::Info, .nextStep = "Write the array from a clocked process if a RAM is intended; state it keeps in logic is reported by the latch rules.", .signals = {name}, .identityKey = "combinational-writer"});
            }
            visitDefault(v);
        }
    };
    void check(Lint& l) override {
        std::map<const Symbol*, std::set<const Symbol*>> nonConstant;
        std::map<const Symbol*, std::vector<MemoryRead>> memoryReads;
        if (!writerRule) {
            MemoryReads reads(memoryReads);
            l.visitHardware(reads);
        }
        {
            slang::ast::ASTContext ac(l.compilation().getRoot(), slang::ast::LookupLocation::max);
            slang::ast::EvalContext ec(ac);
            NonConstantWriters nw(ec);
            l.visitHardware(nw);
            nonConstant = std::move(nw.out);
        }
        if (!writerRule) {
            // a time-zero table fill with a loop counter in its index is not a run-time writer
            for (auto it = nonConstant.begin(); it != nonConstant.end();) {
                for (auto p = it->second.begin(); p != it->second.end();) p = initializationOnly(*p, l) ? it->second.erase(p) : std::next(p);
                it = it->second.empty() ? nonConstant.erase(it) : std::next(it);
            }
        }
        if (!writerRule) mergeCalledReads(l, memoryReads);
        std::set<const Symbol*> registerArrays;
        std::map<const Symbol*, ClockKey> definedPort;
        std::map<const Symbol*, WriteInfo> infos;
        if (!writerRule) classify(l, registerArrays, definedPort);
        else { MemoryWrites writes(l, infos); l.visitHardware(writes); }
        V v(l, writerRule, nonConstant, memoryReads, registerArrays, definedPort, infos);
        l.visitHardware(v);
        if (!writerRule && !v.hasWriter.empty()) reportOpaqueReads(l);
    }
    // A memory whose reads need no collision obligation: written only at elaboration-fixed indices (a register array: a shift register, a pipeline,
    // one word per generate block), or written by one clocked process with nonblocking assignments only (every read sees the word as it was before
    // the clock edge, in simulation). Anything the scan of its writers does not see through keeps the obligation.
    static void classify(Lint& l, std::set<const Symbol*>& registerArrays, std::map<const Symbol*, ClockKey>& definedPort) {
        std::map<const Symbol*, WriteInfo> info;
        MemoryWrites writes(l, info);
        l.visitHardware(writes);
        const auto* am = l.context().design.analysis.get();
        if (!am) return;
        const auto& procedural = model::getModel(l.context().design).proceduralWrites;
        for (const auto& [memory, wi] : info) {
            if (!l.isUser(memory->location)) continue;
            const auto& var = memory->as<VariableSymbol>();
            bool onlyProcedural = true, dynamicDriver = false;
            for (auto* d : am->getDrivers(var)) {
                if (d->isInputPort() || initializationOnly(d->containingSymbol, l)) continue;
                onlyProcedural = onlyProcedural && d->containingSymbol->kind == SymbolKind::ProceduralBlock;
                dynamicDriver = dynamicDriver || d->path.empty() || !d->path.isFullyStatic();
            }
            const bool opaque = std::any_of(procedural.begin(), procedural.end(), [&](const auto& w) {
                return w.called && (w.target == &var || (!w.target && (!w.mayWriteKnown || std::find(w.mayWrite.begin(), w.mayWrite.end(), &var) != w.mayWrite.end())));
            });
            if (!onlyProcedural || opaque || wi.processes.count(nullptr)) continue;
            if (!wi.unstatic && !dynamicDriver) registerArrays.insert(memory);
            else if (wi.processes.size() == 1 && !wi.blocking && !wi.comb && isClocked((*wi.processes.begin())->as<ProceduralBlockSymbol>()))
                definedPort[memory] = clockKeyOf((*wi.processes.begin())->as<ProceduralBlockSymbol>(), model::getModel(l.context().design));
        }
    }
    // Reads found in the syntax of a process get their static word range from the shared read inventory, which also
    // holds the reads inside called subroutine bodies that the syntax of the caller does not show.
    static void mergeCalledReads(Lint& l, std::map<const Symbol*, std::vector<MemoryRead>>& reads) {
        auto rawLoc = [&](slang::SourceLocation at) {
            SourceLocation out;
            if (!at.valid()) return out;
            out.file = l.sm().getFullPath(at.buffer()).generic_string();
            out.line = static_cast<uint32_t>(l.sm().getLineNumber(at));
            out.column = static_cast<uint32_t>(l.sm().getColumnNumber(at));
            return out;
        };
        for (auto& [sym, list] : reads)
            for (auto& r : list) {
                auto start = r.ref->sourceRange.start();
                if (!l.isUser(start)) continue;
                r.loc = l.loc(start);
                r.raw = rawLoc(start);
            }
        std::set<std::pair<const MemoryRead*, int>> seen;
        for (const auto& pr : model::getModel(l.context().design).processReads) {
            if (!pr.target || !pr.process || !l.isUser(pr.process->location)) continue;
            if (auto* p = pr.process->as_if<ProceduralBlockSymbol>(); p &&
                (p->procedureKind == ProceduralBlockKind::Initial || p->procedureKind == ProceduralBlockKind::Final)) continue;
            auto* var = pr.target->as_if<VariableSymbol>();
            if (!var || !isModuleLevel(*var) || !containsMemory(var->getType())) continue;
            auto& list = reads[var];
            bool found = false;
            for (auto& r : list) {
                if (!(r.raw == pr.loc || r.loc == pr.loc)) continue;
                found = true;
                bool first = seen.insert({&r, 0}).second;
                r.exact = (first || r.exact) && pr.exact;
                r.lo = first ? pr.lo : std::min(r.lo, pr.lo);
                r.hi = first ? pr.hi : std::max(r.hi, pr.hi);
            }
            if (!found) list.push_back({nullptr, pr.loc, pr.loc, pr.process, pr.exact, pr.lo, pr.hi});
        }
    }
    // A called subroutine read that cannot be attributed may read any memory; it matters when some memory has a writer.
    static void reportOpaqueReads(Lint& l) {
        std::set<std::tuple<std::string, uint32_t, uint32_t>> sites;
        for (const auto& pr : model::getModel(l.context().design).processReads) {
            if (pr.target || !pr.process || !l.isUser(pr.process->location)) continue;
            if (auto* p = pr.process->as_if<ProceduralBlockSymbol>(); p &&
                (p->procedureKind == ProceduralBlockKind::Initial || p->procedureKind == ProceduralBlockKind::Final)) continue;
            // System-call arguments are read in the syntax of the process and need no attribution here.
            if (pr.reason.rfind("system subroutine", 0) == 0) continue;
            if (!sites.insert({pr.loc.file, pr.loc.line, pr.loc.column}).second) continue;
            l.count();
            l.reportAt(pr.loc, "Unresolved: " + pr.reason + "; a memory with a write process may be read here, so its read-during-write collision is not examined",
                       {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                        .nextStep = "Read the memory in the process itself, or state and verify the intended RAM collision behavior.",
                        .hierarchy = pr.process->getHierarchicalPath()});
        }
    }
};

class MemorySemanticsRule final : public MemoryRule {
public: MemorySemanticsRule() : MemoryRule(false) {}
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-054: registers without reset or initial value that steer control flow
const Symbol* counterpart(const Symbol& sym, const InstanceBodySymbol& to);
class XControlRule final : public LintRule {
public:
    XControlRule() : LintRule("LNT-054") {}
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::set<const Symbol*> unset;                         // registers assigned in clocked code without reset
        std::set<const Symbol*> hasReset;
        std::set<const Symbol*> resetWritten;                  // written in a recognised reset branch (the reset exists)
        std::set<const Symbol*> replaced;                      // ... and a write that may run while it is asserted stores something else
        std::set<const Symbol*> storesData;                    // ... in the reset branch itself: a value that is not constant while the reset is asserted
        // A control use of a register, with the one-bit nets that must be true for it to be evaluated at all: the conjuncts of the
        // condition it sits in and the conditions of the `if` / `?:` branches around it (`if (valid_q) if (data_q) ...`, `valid_q && data_q`).
        struct Use { slang::SourceLocation at; std::set<const Symbol*> flags; };
        std::map<const Symbol*, std::vector<Use>> uses;        // control uses, in the order the design is walked
        std::vector<std::pair<const Expression*, bool>> branches;   // conditions of the branches around the walk, with their truth there
        explicit V(Lint& lint) : l(lint) {}
        // Nets a condition makes true when it evaluates to `truth`: `a && b` true, `!(a || b)`, `!n`, a one-bit `a & b`.
        static void flagsOf(const Expression& cond, bool truth, std::set<const Symbol*>& out, int depth = 0) {
            const Expression& e = unwrapImplicit(cond);
            if (depth > 8 || !e.type || !e.type->isIntegral()) return;
            if (e.kind == ExpressionKind::NamedValue) {
                if (truth && e.type->getBitWidth() == 1) out.insert(&e.as<NamedValueExpression>().symbol);
                return;
            }
            if (e.kind == ExpressionKind::UnaryOp) {
                const auto& u = e.as<UnaryExpression>();
                if (u.op == UnaryOperator::LogicalNot || (u.op == UnaryOperator::BitwiseNot && u.operand().type && u.operand().type->getBitWidth() == 1))
                    flagsOf(u.operand(), !truth, out, depth + 1);
                return;
            }
            if (e.kind != ExpressionKind::BinaryOp) return;
            const auto& b = e.as<BinaryExpression>();
            const bool oneBit = b.left().type && b.right().type && b.left().type->getBitWidth() == 1 && b.right().type->getBitWidth() == 1;
            const bool conjunction = b.op == BinaryOperator::LogicalAnd || (b.op == BinaryOperator::BinaryAnd && oneBit);
            const bool disjunction = b.op == BinaryOperator::LogicalOr || (b.op == BinaryOperator::BinaryOr && oneBit);
            if ((conjunction && truth) || (disjunction && !truth)) {
                flagsOf(b.left(), truth, out, depth + 1);
                flagsOf(b.right(), truth, out, depth + 1);
            }
        }
        std::set<const Symbol*> flagsHere(const Expression& own) const {
            std::set<const Symbol*> flags;
            flagsOf(own, true, flags);
            for (const auto& [cond, truth] : branches) flagsOf(*cond, truth, flags);
            return flags;
        }
        void noteCond(const Expression& e) {
            Reads r;
            e.visit(r);
            const auto flags = flagsHere(e);
            for (auto* s : r.syms) uses[s].push_back({e.sourceRange.start(), flags});
            struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
                V& owner;
                slang::SourceLocation at;
                std::set<const Symbol*> flags;
                std::set<const SubroutineSymbol*> active;
                bool unknown = false;
                Calls(V& v, slang::SourceLocation loc, std::set<const Symbol*> f) : owner(v), at(loc), flags(std::move(f)) {}
                void handle(const CallExpression& call) {
                    visitDefault(call);
                    if (call.isSystemCall()) return;
                    const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
                    if (!sub || call.thisClass() || sub->isVirtual() ||
                        sub->flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) ||
                        sub->getBody().bad() || active.size() >= 32 || !active.insert(sub).second) {
                        unknown = true; return;
                    }
                    Reads reads;
                    sub->getBody().visit(reads);
                    for (auto* symbol : reads.syms)
                        if (isModuleLevel(*symbol)) owner.uses[symbol].push_back({at, flags});
                    sub->getBody().visit(*this);
                    active.erase(sub);
                }
            } calls(*this, e.sourceRange.start(), flags);
            e.visit(calls);
            if (calls.unknown)
                l.report(e.sourceRange.start(), "Unresolved: called condition dependencies cannot be fully attributed",
                         {.confidence = Confidence::Unresolved});
        }
        // The branch a condition selects is evaluated only where the condition holds: its true arm under every condition of the `if`
        // (`&&&` lists them), its false arm only where the single condition is false.
        template <typename Arm>
        void arms(const std::vector<std::pair<const Expression*, bool>>& conds, Arm&& arm) {
            const auto depth = branches.size();
            branches.insert(branches.end(), conds.begin(), conds.end());
            arm();
            branches.resize(depth);
        }
        void handle(const ConditionalStatement& c) {
            if (l.isUser(c.sourceRange.start())) for (auto& cond : c.conditions) noteCond(*cond.expr);
            for (auto& cond : c.conditions) {
                cond.expr->visit(*this);
                if (cond.pattern) cond.pattern->visit(*this);
            }
            const bool plain = std::none_of(c.conditions.begin(), c.conditions.end(), [](const auto& cond) { return cond.pattern; });
            std::vector<std::pair<const Expression*, bool>> whenTrue, whenFalse;
            if (plain) {
                for (auto& cond : c.conditions) whenTrue.push_back({cond.expr, true});
                if (c.conditions.size() == 1) whenFalse.push_back({c.conditions[0].expr, false});
            }
            arms(whenTrue, [&] { c.ifTrue.visit(*this); });
            if (c.ifFalse) arms(whenFalse, [&] { c.ifFalse->visit(*this); });
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) noteCond(c.expr);
            visitDefault(c);
        }
        void handle(const ConditionalExpression& c) {
            if (l.isUser(c.sourceRange.start())) for (auto& cond : c.conditions) noteCond(*cond.expr);
            for (auto& cond : c.conditions) {
                cond.expr->visit(*this);
                if (cond.pattern) cond.pattern->visit(*this);
            }
            const bool plain = std::none_of(c.conditions.begin(), c.conditions.end(), [](const auto& cond) { return cond.pattern; });
            std::vector<std::pair<const Expression*, bool>> whenTrue, whenFalse;
            if (plain) {
                for (auto& cond : c.conditions) whenTrue.push_back({cond.expr, true});
                if (c.conditions.size() == 1) whenFalse.push_back({c.conditions[0].expr, false});
            }
            arms(whenTrue, [&] { c.left().visit(*this); });
            arms(whenFalse, [&] { c.right().visit(*this); });
        }
    };
    void check(Lint& l) override {
        V v(l);
        l.visitHardware(v);
        auto& m = model::getModel(l.context().design);
        std::set<std::tuple<std::string, uint32_t, uint32_t>> calledSites;
        // Fold reset writes in program order, per bit. A later possible write can invalidate a definition.
        // Track undecided reset identities separately so they can only make a missing reset Unresolved.
        using Key = std::tuple<const ProceduralBlockSymbol*, const ValueSymbol*, const ConditionalStatement*>;
        std::map<const ProceduralBlockSymbol*, std::optional<model::SyncReset>> resetOf;
        std::map<Key, WriteRanges> defined, definedIfUndecided;
        std::map<const ValueSymbol*, std::string> undecidedSignal;
        std::set<const Symbol*> initialValues;
        std::set<const Symbol*> unboundResetValues;
        for (const auto& write : m.proceduralWrites) {
            if (!l.isUser(write.process->location)) continue;
            if (write.target && isClocked(*write.process)) {
                v.unset.insert(write.target);
                auto cached = resetOf.find(write.process);
                if (cached == resetOf.end()) cached = resetOf.emplace(write.process, model::synchronousReset(*write.process, m)).first;
                const auto& reset = cached->second;
                auto has = [&](const Statement* st) { return st && std::find(write.scopes.begin(), write.scopes.end(), st) != write.scopes.end(); };
                auto track = [&](const model::ResetBranch& branch, std::map<Key, WriteRanges>& into, bool decided) {
                    if (has(branch.reset)) {
                        if (decided) v.resetWritten.insert(write.target);
                        else undecidedSignal.emplace(write.target, std::string(branch.signal->name));
                    }
                    if (has(branch.other)) return;
                    // in the branch of a disjunction, a write on an arm that does not run while the net is asserted leaves the reset alone
                    const bool inCompound = has(branch.reset) && branch.compound;
                    if (inCompound && model::reachWhileAsserted(write, branch) == model::Reach::Skipped) return;
                    auto& bits = into[{write.process, write.target, branch.test}];
                    auto certain = certainRanges(write, l, has(branch.reset) ? branch.reset : nullptr, inCompound ? &branch : nullptr);
                    // The branch of a disjunction (`rst || load`) also runs without the reset: a write there leaves a constant only if
                    // it stores one while the tested net is asserted (`q <= rst ? 1'b0 : din`).
                    const bool constant = write.constantValue || (has(branch.reset) && branch.compound && model::storesConstantWhileAsserted(write, branch));
                    if (!constant && write.fromCall) unboundResetValues.insert(write.target);
                    if (constant) {
                        if (certain) bits.insert(bits.end(), certain->begin(), certain->end());
                    } else {
                        if (decided) {
                            v.replaced.insert(write.target);
                            if (has(branch.reset)) v.storesData.insert(write.target);
                        }
                        auto taken = writeRanges(write, l);
                        bits = subtractRanges(bits, taken ? *taken : WriteRanges{{write.lo, write.hi}});
                    }
                };
                if (reset) {
                    for (const auto& branch : reset->known) track(branch, defined, true);
                    for (const auto& branch : reset->undecided) track(branch, definedIfUndecided, false);
                }
            }
            if (write.called && isClocked(*write.process) && calledSites.insert({write.loc.file, write.loc.line, write.loc.column}).second)
                l.reportAt(write.loc, "Unresolved: " + write.reason,
                           {.confidence = Confidence::Unresolved, .hierarchy = write.process->getHierarchicalPath()});
        }
        auto covered = [](const std::map<Key, WriteRanges>& from, std::set<const ValueSymbol*>& out) {
            std::map<const ValueSymbol*, WriteRanges> bitsOf;
            for (auto& [key, bits] : from) bitsOf[std::get<1>(key)].insert(bitsOf[std::get<1>(key)].end(), bits.begin(), bits.end());
            for (auto& [target, bits] : bitsOf) {
                uint64_t width = target->getType().isFixedSize() ? target->getType().getBitstreamWidth() : 0;
                if (width && rangesCover(bits, WriteRanges{{0, width - 1}})) out.insert(target);
            }
        };
        std::set<const ValueSymbol*> resetByKnown, resetIfUndecided;
        covered(defined, resetByKnown);
        covered(definedIfUndecided, resetIfUndecided);
        for (auto* target : resetByKnown) v.hasReset.insert(target);
        {
            // A time-zero initial block that leaves a constant in the whole object, written by one block of plain
            // blocking assignments, is an initial value like a declaration initialiser; whether the target honours
            // it is LNT-048's finding (WARNING for ASIC), not this rule's.
            std::map<const Symbol*, std::set<const ProceduralBlockSymbol*>> initialBlocks;
            std::map<const Symbol*, bool> initialOk;
            for (const auto& write : m.proceduralWrites)
                if (write.target && write.process->procedureKind == ProceduralBlockKind::Initial) {
                    initialBlocks[write.target].insert(write.process);
                    initialOk[write.target] = write.wholeConstant && !write.nonBlocking && !write.fromCall && write.conditionalDepth == 0;
                }
            std::function<bool(const Statement&)> timeZero = [&](const Statement& st) {
                if (st.kind == StatementKind::Block) return timeZero(st.as<BlockStatement>().body);
                if (st.kind == StatementKind::List) {
                    for (auto* child : st.as<StatementList>().list) if (!timeZero(*child)) return false;
                    return true;
                }
                return st.kind == StatementKind::ExpressionStatement || st.kind == StatementKind::VariableDeclaration ||
                       st.kind == StatementKind::Empty;
            };
            for (auto& [target, blocks] : initialBlocks)
                if (blocks.size() == 1 && initialOk[target] && timeZero((*blocks.begin())->getBody())) initialValues.insert(target);
        }
        // A register read only where a valid flag is set, the flag being a reset flop that is set only by an event that loads the
        // register on the same clock edge (`if (ack) data_q <= d;` beside `if (ack) seen_q <= 1;` or `seen_q <= ack;`), is loaded before
        // any read: with the flag clear the guarded condition is a constant 0 and no X steers control. Every write of the flag must be
        // a constant 0 or imply (through the conditions around it and the value it stores) the conditions of one whole, certain,
        // nonblocking load of the register in a process on the same clock.
        struct Literal { std::string key; bool truth; };
        std::map<const Symbol*, std::vector<const model::ProceduralWrite*>> writesTo;
        std::set<const Symbol*> writtenByCall;
        // Blocking temporaries of clocked processes: their value changes between the statements of one procedure, so a condition that
        // names one is not the same condition at two places.
        std::set<const Symbol*> blockingTargets;
        bool opaqueCall = false;
        for (const auto& write : m.proceduralWrites) {
            if (write.target) writesTo[write.target].push_back(&write);
            if (write.target && !write.nonBlocking && write.process && isClocked(*write.process)) blockingTargets.insert(write.target);
            if (write.called && write.process && isClocked(*write.process)) {
                if (!write.mayWriteKnown) opaqueCall = true;
                for (const auto* t : write.mayWrite) writtenByCall.insert(t);
            }
        }
        std::function<std::optional<std::string>(const Expression&)> keyOf = [&](const Expression& raw) -> std::optional<std::string> {
            const Expression& e = unwrapImplicit(raw);
            if (const auto c = l.evalConst(e); c.isInteger() && !c.integer().hasUnknown()) return "c" + c.integer().toString();
            switch (e.kind) {
                case ExpressionKind::NamedValue: {
                    const auto& sym = e.as<NamedValueExpression>().symbol;
                    if (blockingTargets.count(&sym)) return std::nullopt;
                    return "s" + std::to_string(reinterpret_cast<uintptr_t>(&sym));
                }
                case ExpressionKind::UnaryOp: {
                    const auto& u = e.as<UnaryExpression>();
                    const auto inner = keyOf(u.operand());
                    return inner ? std::optional<std::string>("u" + std::to_string(static_cast<int>(u.op)) + "(" + *inner + ")") : std::nullopt;
                }
                case ExpressionKind::BinaryOp: {
                    const auto& b = e.as<BinaryExpression>();
                    const auto x = keyOf(b.left()), y = keyOf(b.right());
                    return x && y ? std::optional<std::string>("b" + std::to_string(static_cast<int>(b.op)) + "(" + *x + "," + *y + ")") : std::nullopt;
                }
                case ExpressionKind::ElementSelect: {
                    const auto& sel = e.as<ElementSelectExpression>();
                    const auto x = keyOf(sel.value()), y = keyOf(sel.selector());
                    return x && y ? std::optional<std::string>("e(" + *x + "," + *y + ")") : std::nullopt;
                }
                default: return std::nullopt;
            }
        };
        // The conjuncts a condition adds when it evaluates to `truth`. False when one of them has no key (the caller then knows nothing
        // of that part, which a requirement cannot ignore).
        std::function<bool(const Expression&, bool, std::vector<Literal>&)> literals = [&](const Expression& raw, bool truth, std::vector<Literal>& out) {
            const Expression& e = unwrapImplicit(raw);
            if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot)
                return literals(e.as<UnaryExpression>().operand(), !truth, out);
            if (e.kind == ExpressionKind::BinaryOp) {
                const auto& b = e.as<BinaryExpression>();
                if ((b.op == BinaryOperator::LogicalAnd && truth) || (b.op == BinaryOperator::LogicalOr && !truth)) {
                    const bool x = literals(b.left(), truth, out);
                    return literals(b.right(), truth, out) && x;
                }
            }
            const auto key = keyOf(e);
            if (!key) return false;
            out.push_back({*key, truth});
            return true;
        };
        // What must hold for `write` to run: the conditions of the `if` branches around it. `exact` (a register's load) fails on any
        // condition not written down; otherwise an unknown part is left out, which only weakens what a flag write implies.
        const auto pathOf = [&](const model::ProceduralWrite& write, bool exact) -> std::optional<std::vector<Literal>> {
            std::vector<Literal> out;
            for (size_t i = 0; i < write.scopes.size(); ++i) {
                const Statement* st = write.scopes[i];
                if (st->kind != StatementKind::Conditional) {
                    if (exact) return std::nullopt;
                    continue;
                }
                const auto& c = st->as<ConditionalStatement>();
                const Statement* arm = i + 1 < write.scopes.size() ? write.scopes[i + 1] : nullptr;
                const bool plain = std::none_of(c.conditions.begin(), c.conditions.end(), [](const auto& cond) { return cond.pattern; });
                if (arm == &c.ifTrue && plain) {
                    for (const auto& cond : c.conditions)
                        if (!literals(*cond.expr, true, out) && exact) return std::nullopt;
                }
                else if (c.ifFalse && arm == c.ifFalse && plain && c.conditions.size() == 1) {
                    if (!literals(*c.conditions[0].expr, false, out) && exact) return std::nullopt;
                }
                else if (exact) return std::nullopt;
                ++i;
            }
            return out;
        };
        // The clock of a clocked process: its one edge event on a net that is not a reset.
        const auto clockOf = [&](const ProceduralBlockSymbol& p) -> std::optional<std::pair<const Symbol*, int>> {
            const Statement& body = p.getBody();
            if (body.kind != StatementKind::Timed) return std::nullopt;
            std::vector<std::pair<const Symbol*, int>> events;
            bool opaque = false;
            std::function<void(const TimingControl&)> collect = [&](const TimingControl& t) {
                if (t.kind == TimingControlKind::EventList) { for (const auto* e : t.as<EventListControl>().events) collect(*e); return; }
                if (t.kind != TimingControlKind::SignalEvent) { opaque = true; return; }
                const auto& ev = t.as<SignalEventControl>();
                const auto* sym = unwrapImplicit(ev.expr).getSymbolReference();
                if (!sym || ev.edge == EdgeKind::None || ev.iffCondition) { opaque = true; return; }
                if (const auto* value = sym->as_if<ValueSymbol>(); value && model::classifyReset(m, *value).reset) return;
                events.push_back({sym, static_cast<int>(ev.edge)});
            };
            collect(body.as<TimedStatement>().timing);
            if (opaque || events.size() != 1) return std::nullopt;
            return events[0];
        };
        // The value a load stores is defined: no unknown literal, and it reads neither the register itself nor a register that is itself
        // without a reset or initial value (`word_q <= word_q + 1`, a stage fed by an unreset stage). Combinational nets are taken as
        // defined: a path from an unreset register through logic is not followed.
        struct UnknownLiteral : ASTVisitor<UnknownLiteral, VisitFlags::AllGood> {
            bool found = false;
            void handle(const IntegerLiteral& e) { found = found || e.getValue().hasUnknown(); }
            void handle(const UnbasedUnsizedIntegerLiteral& e) { found = found || e.getValue().hasUnknown(); }
        };
        const auto definedValue = [&](const Expression& value, const Symbol& reg) -> bool {
            UnknownLiteral lit;
            value.visit(lit);
            if (lit.found) return false;
            Reads reads;
            value.visit(reads);
            for (const auto* sym : reads.syms) {
                if (sym == &reg) return false;
                if (!v.unset.count(sym) || v.hasReset.count(sym) || initialValues.count(sym)) continue;
                const auto* var = sym->as_if<VariableSymbol>();
                if (!var || !declarationInitializer(*var)) return false;
            }
            return true;
        };
        const auto after = [](const model::ProceduralWrite& a, const model::ProceduralWrite& b) {
            return a.loc.file == b.loc.file && std::make_pair(a.loc.line, a.loc.column) > std::make_pair(b.loc.line, b.loc.column);
        };
        const auto* analysis = l.context().design.analysis.get();
        const auto preloaded = [&](const Symbol& flagSeen, const Symbol& reg) -> bool {
            if (opaqueCall || !analysis) return false;
            const InstanceBodySymbol* body = enclosingBody(reg);
            const Symbol* flag = body && enclosingBody(flagSeen) != body ? counterpart(flagSeen, *body) : &flagSeen;
            const auto* flagValue = flag ? flag->as_if<ValueSymbol>() : nullptr;
            const auto* regValue = reg.as_if<ValueSymbol>();
            if (!flagValue || !regValue || flag == &reg || flagValue->getType().getBitWidth() != 1 || !v.hasReset.count(flag) ||
                writtenByCall.count(flag))
                return false;
            if (const auto* var = flag->as_if<VariableSymbol>(); !var || declarationInitializer(*var)) return false;
            for (const auto* driver : analysis->getDrivers(*flagValue)) {
                const auto* proc = driver->kind == analysis::DriverKind::Procedural ? driver->containingSymbol->as_if<ProceduralBlockSymbol>() : nullptr;
                if (!proc || !isClocked(*proc)) return false;
            }
            const auto flagWrites = writesTo.find(flag);
            if (flagWrites == writesTo.end()) return false;
            // The loads of the register that certainly run when their conditions hold.
            struct Load { std::vector<Literal> when; std::optional<std::pair<const Symbol*, int>> clock; };
            std::vector<Load> loads;
            const auto regWrites = writesTo.find(&reg);
            if (regWrites == writesTo.end()) return false;
            const uint64_t width = regValue->getType().isFixedSize() ? regValue->getType().getBitstreamWidth() : 0;
            for (const auto* w : regWrites->second) {
                if (!w->process || !isClocked(*w->process) || !w->nonBlocking || w->called || w->fromCall || w->afterPossibleExit ||
                    w->partialTarget || !w->exactTarget || !width || w->lo != 0 || w->hi + 1 != width)
                    continue;
                if (!w->value || !definedValue(*w->value, reg)) continue;
                // A later write of the register in the same procedure may replace what the load stored.
                if (std::any_of(regWrites->second.begin(), regWrites->second.end(), [&](const model::ProceduralWrite* o) {
                        return o != w && o->process == w->process && after(*o, *w); }))
                    continue;
                if (auto when = pathOf(*w, true)) loads.push_back({std::move(*when), clockOf(*w->process)});
            }
            if (loads.empty()) return false;
            for (const auto* w : flagWrites->second) {
                if (!w->process || !isClocked(*w->process) || !w->nonBlocking || w->called || w->fromCall || w->partialTarget || !w->value)
                    return false;
                const auto stored = l.evalConst(*w->value);
                if (stored.isInteger() && !stored.integer().hasUnknown() && stored.isFalse()) continue;   // a clear
                auto facts = pathOf(*w, false);
                if (!facts) return false;
                literals(*w->value, true, *facts);
                const auto clock = clockOf(*w->process);
                const bool implies = std::any_of(loads.begin(), loads.end(), [&](const Load& load) {
                    if (!clock || load.clock != clock) return false;
                    return std::all_of(load.when.begin(), load.when.end(), [&](const Literal& need) {
                        return std::any_of(facts->begin(), facts->end(), [&](const Literal& f) { return f.key == need.key && f.truth == need.truth; });
                    });
                });
                if (!implies) return false;
            }
            return true;
        };
        for (auto* s : v.unset) {
            if (v.hasReset.count(s)) continue;
            auto* var = s->as_if<VariableSymbol>();
            if ((var && declarationInitializer(*var)) || initialValues.count(s)) continue;
            auto it = v.uses.find(s);
            if (it == v.uses.end()) {
                // slang shares the analysed body of identical instances and the visitor walks that one only: the uses of a register in
                // another instance are those of its counterpart there.
                const InstanceBodySymbol* body = enclosingBody(*s);
                const InstanceBodySymbol* canon = body && body->parentInstance ? body->parentInstance->getCanonicalBody() : nullptr;
                if (canon && canon != body)
                    if (const Symbol* twin = counterpart(*s, *canon)) it = v.uses.find(twin);
            }
            if (it == v.uses.end()) continue;
            l.count();
            // The first control use that is not guarded by a flag that loads the register first.
            const V::Use* firstUse = nullptr;
            for (const auto& u : it->second) {
                const bool guarded = std::any_of(u.flags.begin(), u.flags.end(), [&](const Symbol* f) { return preloaded(*f, *s); });
                if (!guarded) { firstUse = &u; break; }
            }
            if (!firstUse) continue;
            const auto at = firstUse->at;
            if (auto* vs = s->as_if<ValueSymbol>(); vs && resetIfUndecided.count(vs)) {
                l.context().degradedReason = "reset identity is undecided";
                l.report(at, "Unresolved: register '" + std::string(s->name) + "' is reset only by '" + undecidedSignal[vs] +
                                         "', whose reset identity is undecided (not clearly a reset name, no asynchronous reset edge, or instances disagree)",
                         {.confidence = Confidence::Unresolved, .nextStep = "Name the reset like a reset, or use it as an asynchronous reset.",
                          .hierarchy = s->getHierarchicalPath(), .signals = {std::string(s->name)}});
                continue;
            }
            if (unboundResetValues.count(s)) {
                l.report(at, "Unresolved: reset value of register '" + std::string(s->name) +
                         "' depends on a called write whose value is not established in the caller context",
                         {.confidence = Confidence::Unresolved, .hierarchy = s->getHierarchicalPath(), .signals = {std::string(s->name)}});
                l.context().degradedReason = "called reset-write value is not established in the caller context";
                continue;
            }
            const std::string reg = "register '" + std::string(s->name) + "'";
            const std::string text = v.storesData.count(s)
                ? reg + " is not left a constant while its reset is asserted: the reset branch stores a value that is not a constant then, so its value is X until first assigned and it steers control flow here"
                : v.resetWritten.count(s) && v.replaced.count(s)
                ? "register '" + std::string(s->name) + "' is not left a constant while its reset is asserted: a later write may replace the reset value, so its value is X until first assigned and it steers control flow here"
                : "register '" + std::string(s->name) + "' has no reset or initial value but steers control flow here: its value is X until first assigned";
            l.report(at, text,
                     {.confidence = Confidence::Heuristic, .nextStep = "Reset the register, or guard the control with a valid flag.", .hierarchy = s->getHierarchicalPath(), .signals = {std::string(s->name)}});
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-060: a register assigned with a blocking assignment in a clocked process and read in another process (clocked, combinational or a
// continuous assignment)
class RaceRule final : public LintRule {
public:
    RaceRule() : LintRule("LNT-060") {}

private:
    // Port connections as the same storage: `.o(w)` makes the child's `o` and the parent's `w` one net. `exact` when the whole of both
    // are connected (same flat bits); a select, concatenation or width change links them without saying which bits.
    struct Link { const Symbol* other; bool exact; };
    using Links = std::map<const Symbol*, std::vector<Link>>;
    struct Connections : HardwareVisitor<Connections, VisitFlags::AllCanonical> {
        Links& links;
        explicit Connections(Links& out) : links(out) {}
        static const Expression& bare(const Expression& e) {
            const Expression* x = &e;
            for (;;) {
                if (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
                else if (x->kind == ExpressionKind::Assignment) x = &x->as<AssignmentExpression>().left();
                else return *x;
            }
        }
        void handle(const InstanceSymbol& inst) {
            for (const auto* pc : inst.getPortConnections()) {
                if (!pc || pc->port.kind != SymbolKind::Port) continue;
                const auto* internal = pc->port.as<PortSymbol>().internalSymbol;
                const Expression* expr = pc->getExpression();
                if (!internal || !expr || !internal->as_if<ValueSymbol>()) continue;
                const Expression& e = bare(*expr);
                std::vector<const Symbol*> roots;
                lvalueSymbols(e, roots);
                const bool whole = e.kind == ExpressionKind::NamedValue && roots.size() == 1 &&
                                   roots[0]->as<ValueSymbol>().getType().getBitWidth() == internal->as<ValueSymbol>().getType().getBitWidth();
                for (const auto* r : roots) {
                    if (!r->as_if<ValueSymbol>() || r == internal) continue;
                    links[internal].push_back({r, whole});
                    links[r].push_back({internal, whole});
                }
            }
            visitDefault(inst);
        }
    };
    // A procedure whose only statement is a concurrent assertion: it samples its operands, it does not race with a write.
    static bool assertionOnly(const Symbol& process) {
        const auto* p = process.as_if<ProceduralBlockSymbol>();
        if (!p) return false;
        const Statement* s = &p->getBody();
        while (s) {
            if (s->kind == StatementKind::ConcurrentAssertion) return true;
            if (s->kind == StatementKind::Timed) s = &s->as<TimedStatement>().stmt;
            else if (s->kind == StatementKind::Block) s = &s->as<BlockStatement>().body;
            else return false;
        }
        return false;
    }
    void check(Lint& l) override {
        Links links;
        {
            Connections c(links);
            l.visitHardware(c);
        }
        const auto& writes = model::getModel(l.context().design).proceduralWrites;
        std::map<const Symbol*, std::vector<const model::ProceduralWrite*>> writers;
        std::set<std::tuple<std::string, unsigned, unsigned>> unknownSites;
        bool uncertainWriter = false;
        for (const auto& w : writes) {
            if (!w.process || !l.isUser(w.process->location) || !isClocked(*w.process)) continue;
            if (w.called) {
                if (w.copyBackValueUnknown) continue;
                uncertainWriter = true;
                if (unknownSites.emplace(w.loc.file, w.loc.line, w.loc.column).second) {
                    l.reportAt(w.loc, "Unresolved: blocking writer effects cannot be established: " + w.reason,
                               {.confidence = Confidence::Unresolved, .hierarchy = w.process->getHierarchicalPath()});
                    l.context().degradedReason = "clocked procedure write effects are unresolved";
                }
            } else if (!w.nonBlocking && !w.throughCopyFormal && w.target && isModuleLevel(*w.target)) {
                writers[w.target].push_back(&w);
            }
        }
        // A system subroutine or built-in method that may change storage is a blocking writer of unknown extent.
        std::deque<model::ProceduralWrite> systemWrites;
        for (const auto& sw : model::getModel(l.context().design).systemWrites) {
            if (!sw.process || !l.isUser(sw.process->location) || !isClocked(*sw.process) || !sw.target || !isModuleLevel(*sw.target)) continue;
            model::ProceduralWrite w;
            w.process = sw.process; w.instance = sw.instance; w.target = sw.target; w.loc = sw.loc;
            w.forkOwnership = sw.forkOwnership; w.lo = sw.lo; w.hi = sw.hi; w.exactTarget = false; w.partialTarget = true;
            w.reason = "system subroutine '" + sw.name + "' may write its argument";
            writers[w.target].push_back(&systemWrites.emplace_back(std::move(w)));
        }
        for (const auto& w : model::getModel(l.context().design).copyBackWrites) {
            if (!w.process || !l.isUser(w.process->location) || !isClocked(*w.process) || !w.target) continue;
            if (w.called) {
                uncertainWriter = true;
                l.reportAt(w.loc, "Unresolved: output copy-back may not return or its effects are opaque",
                           {.confidence = Confidence::Unresolved, .hierarchy = w.process->getHierarchicalPath(), .signals = {std::string(w.target->name)}});
                l.context().degradedReason = "output copy-back is unresolved";
            } else writers[w.target].push_back(&w);
        }
        std::set<const Symbol*> done;
        for (const auto& read : model::getModel(l.context().design).processReads) {
            if (!read.process || !l.isUser(read.process->location)) continue;
            if (auto* p = read.process->as_if<ProceduralBlockSymbol>(); p &&
                (p->procedureKind == ProceduralBlockKind::Initial || p->procedureKind == ProceduralBlockKind::Final)) continue;
            if (assertionOnly(*read.process)) continue;
            if (!read.target) {
                if (writers.empty() && !uncertainWriter) continue;
                l.reportAt(read.loc, "Unresolved: " + read.reason, {.confidence = Confidence::Unresolved, .hierarchy = read.process->getHierarchicalPath()});
                l.context().degradedReason = "called read effects are unresolved";
                continue;
            }
            const auto* sym = read.target;
            auto* proc = read.process;
            const auto& loc = read.loc;
            if (done.count(sym)) continue;
            // The read's storage and everything connected to it through ports (transitively); a link that is not a whole-for-whole
            // connection makes the writer a possible one only.
            std::vector<std::pair<const Symbol*, bool>> storage{{sym, true}};
            for (size_t i = 0; i < storage.size() && storage.size() < 64; i++)
                if (auto lk = links.find(storage[i].first); lk != links.end())
                    for (const auto& link : lk->second)
                        if (std::none_of(storage.begin(), storage.end(), [&](const auto& x) { return x.first == link.other; }))
                            storage.push_back({link.other, storage[i].second && link.exact});
            const model::ProceduralWrite* candidate = nullptr;
            bool viaPort = false, linkExact = true;
            const Symbol* writtenAs = sym;
            for (const auto& [alias, aliasExact] : storage) {
                auto it = writers.find(alias);
                if (it == writers.end()) continue;
                for (auto* w : it->second) {
                    if (w->process == proc && !w->forkOwnership && !read.forkOwnership) continue;
                    if (alias == sym && read.bounded && (w->hi < read.lo || read.hi < w->lo)) continue;
                    const bool better = !candidate || (alias == sym && viaPort) ||
                                        (alias == sym && w->exactTarget && read.exact && !w->forkOwnership);
                    if (better) { candidate = w; viaPort = alias != sym; linkExact = aliasExact; writtenAs = alias; }
                    if (alias == sym && w->exactTarget && read.exact && !w->forkOwnership) break;
                }
            }
            if (!candidate) continue;
            done.insert(sym);
            l.count();
            if (!candidate->exactTarget || !read.exact || candidate->forkOwnership || read.forkOwnership || !linkExact) {
                l.reportAt(loc, "Unresolved: a blocking write may overlap or have an independent fork relationship with the read of '" + std::string(sym->name) + "' in another process",
                         {.confidence = Confidence::Unresolved, .hierarchy = sym->getHierarchicalPath(), .signals = {std::string(sym->name)}});
                l.context().degradedReason = "blocking write and read overlap is not established";
            } else if (viaPort) {
                l.reportAt(loc, "'" + std::string(writtenAs->name) + "' is assigned with a blocking assignment in a clocked process and read as the connected '" + std::string(sym->name) +
                                    "' in another process: this is a race candidate",
                         {.nextStep = "Use a nonblocking assignment for state shared between clocked processes.", .hierarchy = sym->getHierarchicalPath(), .signals = {std::string(sym->name)}});
            } else {
                l.reportAt(loc, "'" + std::string(sym->name) + "' is assigned with a blocking assignment in a clocked process and read in another process: this is a race candidate",
                         {.nextStep = "Use a nonblocking assignment for state shared between clocked processes.", .hierarchy = sym->getHierarchicalPath(), .signals = {std::string(sym->name)}});
            }
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-010: unconnected ports. slang reports missing and named-empty connections; the positional empty connection
// `inst(a, , c)` is added here (slang diagnostics stay the primary source).
class UnconnectedRule final : public LintRule {
public:
    UnconnectedRule() : LintRule("LNT-010", "", true) {}
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        bool outputsAdvisory;
        explicit V(Lint& lint) : l(lint), outputsAdvisory(lint.option("empty_output", "info") == "info") {}
        void handle(const InstanceSymbol& inst) {
            if (l.isUser(inst.location)) {
                l.count();
                if (auto* syn = inst.getSyntax(); syn && syn->kind == syntax::SyntaxKind::HierarchicalInstance) {
                    auto& hi = syn->as<syntax::HierarchicalInstanceSyntax>();
                    size_t idx = 0;
                    auto conns = inst.getPortConnections();
                    for (auto* pc : hi.connections) {
                        if (pc->kind == syntax::SyntaxKind::EmptyPortConnection) {
                            std::string port = idx < conns.size() && conns[idx] ? std::string(conns[idx]->port.name) : "#" + std::to_string(idx);
                            const bool output = idx < conns.size() && conns[idx] && conns[idx]->port.kind == SymbolKind::Port &&
                                                conns[idx]->port.as<PortSymbol>().direction == ArgumentDirection::Out;
                            l.report(pc->sourceRange().start(), "port '" + port + "' of instance '" + std::string(inst.name) + "' is left empty in the positional connection list" +
                                                                    (output && outputsAdvisory ? std::string(kAdvisory) : ""),
                                     {.severity = output && outputsAdvisory ? Severity::Info : Severity::Warning,
                                      .nextStep = "Connect the port or tie it off explicitly.", .hierarchy = inst.getHierarchicalPath(), .signals = {port}});
                        }
                        if (pc->kind == syntax::SyntaxKind::OrderedPortConnection || pc->kind == syntax::SyntaxKind::EmptyPortConnection) idx++;
                    }
                }
            }
            visitDefault(inst);
        }
    };
    static constexpr const char* kAdvisory = " (an explicitly empty output: advisory under LNT-010.empty_output=info)";
    void check(Lint& l) override {
        visitDesign<V>(l);
        // Policy: an output written as `.q()` is deliberately left open. An empty input or inout is never demoted.
        if (l.option("empty_output", "info") != "info") return;
        for (auto& d : l.context().report.mutableDiagnostics())
            if (d.ruleId == "LNT-010" && d.origin == "slang" && d.upstreamCode == "empty-output-connection" && d.severity == Severity::Warning) {
                d.severity = Severity::Info;
                d.message += kAdvisory;
                d.applyDefaultDisposition();
            }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-011: unused nets/variables/parameters/ports come from slang; a deliberate-unused marker silences slang, so the
// marker itself is reported (nothing is suppressed invisibly)
class UnusedRule final : public LintRule {
public:
    UnusedRule() : LintRule("LNT-011", "", true) {}
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void mark(const Symbol& s) {
            if (!l.isUser(s.location)) return;
            for (auto* a : l.compilation().getAttributes(s)) {
                std::string n(a->name);
                if (n == "unused" || n == "maybe_unused") {
                    l.count();
                    l.report(s, "'" + std::string(s.name) + "' is marked (* " + n + " *): slang does not report it as unused; the marker is the review record",
                             {.severity = Severity::Info, .confidence = Confidence::Informational, .nextStep = "Keep the marker only if the object is intentionally unused.", .signals = {std::string(s.name)}});
                    return;
                }
            }
        }
        void handle(const VariableSymbol& v) { mark(v); visitDefault(v); }
        void handle(const NetSymbol& v) { mark(v); visitDefault(v); }
        void handle(const ParameterSymbol& v) { mark(v); visitDefault(v); }
    };
    void check(Lint& l) override {
        visitDesign<V>(l);
        // A definition the top never instantiates is a fact about the file list, not the design: library files bring
        // many. It stays reported, as INFO, and says it was not linted (lint walks only elaborated hardware).
        for (auto& d : l.context().report.mutableDiagnostics())
            if (d.ruleId == "LNT-011" && d.origin == "slang" && d.upstreamCode == "unused-def" && d.severity == Severity::Warning) {
                d.severity = Severity::Info;
                d.message += " (not instantiated under the top, so not linted)";
                d.applyDefaultDisposition();
            }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-009: variables with several drivers are slang's diagnostics. Nets (wire, tri, tri0, tri1, trireg) with overlapping continuous drivers
// are reported here: ERROR-free WARNING Established when none of the drivers can release the net (no all-z arm; x and partly-z arms still drive); when a driver can
// release it (a tri-state bus) the overlap is a conflict unless the enables are provably exclusive, so the finding is WARNING Unresolved
// ("mutual exclusion not proven"), never silence. wand/wor/triand/trior nets resolve overlaps by definition. Instance-output overlaps are
// in reportOutputPortOverlaps (lint_struct.cpp), which needs that file's driver helpers.
// A literal with every bit z releases the net; partial z (8'b0000_zzzz) and x literals still drive bits.
bool allZ(const SVInt& v) {
    if (v.getBitWidth() == 0) return false;
    for (uint32_t i = 0; i < v.getBitWidth(); i++)
        if (v[static_cast<int32_t>(i)].value != slang::logic_t::z.value) return false;
    return true;
}
// A literal or a named constant (parameter, localparam, enum value, package constant) with every bit z.
struct ZSearchV : ASTVisitor<ZSearchV, VisitFlags::AllGood> {
    Lint& l;
    bool unknown = false;
    explicit ZSearchV(Lint& lint) : l(lint) {}
    void handle(const IntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
    void handle(const UnbasedUnsizedIntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
    void handle(const NamedValueExpression& e) {
        if (e.symbol.kind != SymbolKind::Parameter && e.symbol.kind != SymbolKind::EnumValue) return;
        auto cv = l.evalConst(e);
        unknown = unknown || (cv.isInteger() && allZ(cv.integer()));
    }
};
bool ZSearchFn(Lint& l, const Expression& e) {
    ZSearchV z(l);
    e.visit(z);
    return z.unknown;
}

// `en ? v : 'z` against `!en ? w : 'z` (or a different constant of the same operand): the two tri-state drivers are never enabled together.
bool provablyExclusiveEnables(Lint& l, const Expression& a, const Expression& b) {
    auto cond = [](const Expression& asg) -> const ConditionalExpression* {
        if (asg.kind != ExpressionKind::Assignment) return nullptr;
        const Expression* r = &asg.as<AssignmentExpression>().right();
        while (r->kind == ExpressionKind::Conversion) r = &r->as<ConversionExpression>().operand();
        return r->kind == ExpressionKind::ConditionalOp ? &r->as<ConditionalExpression>() : nullptr;
    };
    auto* ca = cond(a);
    auto* cb = cond(b);
    if (!ca || !cb || ca->conditions.size() != 1 || cb->conditions.size() != 1) return false;
    // the enabled arm is the one without the z literal; assume the true arm drives (`en ? v : 'z`) and the false arm releases
    auto releases = [&](const Expression& arm) { return ZSearchFn(l, arm); };
    bool aTrueDrives = !releases(ca->left()) && releases(ca->right());
    bool bTrueDrives = !releases(cb->left()) && releases(cb->right());
    if (!aTrueDrives || !bTrueDrives) return false;
    slang::ast::ASTContext ac(l.compilation().getRoot(), slang::ast::LookupLocation::max);
    slang::ast::EvalContext ec(ac);
    return sverity::flow::conditionsExclusive(ec, *ca->conditions[0].expr, true, *cb->conditions[0].expr, true);
}

// `bufif1 (y, a, e); bufif0 (y, b, e);`: tri-state gates (bufif/notif) with the same enable signal and opposite enable polarity are
// never enabled together.
bool gatesExclusive(const PrimitiveInstanceSymbol& a, const PrimitiveInstanceSymbol& b) {
    auto polarity = [](std::string_view k) { return k == "bufif1" || k == "notif1" ? 1 : k == "bufif0" || k == "notif0" ? 0 : -1; };
    const int pa = polarity(a.primitiveType.name), pb = polarity(b.primitiveType.name);
    if (pa < 0 || pb < 0 || pa == pb) return false;
    auto enable = [](const PrimitiveInstanceSymbol& g) -> const Symbol* {
        auto ports = g.getPortConnections();
        if (ports.size() != 3) return nullptr;
        const Expression* e = ports[2];
        while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
        return e->kind == ExpressionKind::NamedValue ? &e->as<NamedValueExpression>().symbol : nullptr;
    };
    const Symbol* ea = enable(a);
    return ea && ea == enable(b);
}

// The right-hand side is the constant high-impedance value (every bit z): the driver never drives the net.
bool constantZ(Lint& l, const Expression& asg) {
    if (asg.kind != ExpressionKind::Assignment) return false;
    auto cv = l.evalConst(asg.as<AssignmentExpression>().right());
    if (!cv.isInteger() || cv.integer().getBitWidth() == 0) return false;
    auto& v = cv.integer();
    for (uint32_t i = 0; i < v.getBitWidth(); i++)
        if (v[static_cast<int32_t>(i)].value != slang::logic_t::z.value) return false;
    return true;
}

// A gate that can release its output: a tri-state buffer, a switch or a pass gate. A logic gate or a UDP always drives.
bool gateReleases(std::string_view kind) {
    return kind == "bufif0" || kind == "bufif1" || kind == "notif0" || kind == "notif1" ||
           kind == "nmos" || kind == "pmos" || kind == "rnmos" || kind == "rpmos" || kind == "cmos" || kind == "rcmos" ||
           kind == "tran" || kind == "rtran" || kind == "tranif0" || kind == "tranif1" || kind == "rtranif0" || kind == "rtranif1";
}

// The strength a driver gives a net for a 0 and for a 1, 4 (supply) down to 0 (highz); a driver with none given is strong.
using DriveRank = std::pair<int, int>;
DriveRank driveRank(const std::pair<std::optional<DriveStrength>, std::optional<DriveStrength>>& s) {
    auto rank = [](const std::optional<DriveStrength>& v) {
        if (!v) return 3;
        switch (*v) {
            case DriveStrength::Supply: return 4;
            case DriveStrength::Strong: return 3;
            case DriveStrength::Pull: return 2;
            case DriveStrength::Weak: return 1;
            case DriveStrength::HighZ: return 0;
        }
        return 3;
    };
    return {rank(s.first), rank(s.second)};
}

// What the parent connects to an input port net: null when it is left open or the port belongs to a top module.
const Expression* parentConnection(const NetSymbol& n) {
    const auto* body = n.getParentScope() ? n.getParentScope()->asSymbol().as_if<InstanceBodySymbol>() : nullptr;
    if (!body || !body->parentInstance) return nullptr;
    for (const auto* pc : body->parentInstance->getPortConnections())
        if (pc && pc->port.kind == SymbolKind::Port && pc->port.as<PortSymbol>().internalSymbol == &n) return pc->getExpression();
    return nullptr;
}

// The parent side of an input port may release the net: a z in the connection, or a net there that some driver can release, that
// is an inout, that an instance output drives, or that is itself fed through such a port. Anything not decided counts as releasing.
bool connectionCanRelease(Lint& l, const Expression& conn, int depth = 0) {
    if (ZSearchFn(l, conn)) return true;
    bool release = false;
    conn.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
        if (release || sym.kind != SymbolKind::Net) return;
        const auto& net = sym.as<NetSymbol>();
        if (net.netType.netKind != NetType::Wire) { release = true; return; }
        for (const auto* b = net.getFirstPortBackref(); b; b = b->getNextBackreference())
            if (b->port->direction == ArgumentDirection::InOut) { release = true; return; }
        auto* am = l.context().design.analysis.get();
        if (!am || depth > 8) { release = true; return; }
        for (auto* d : am->getDrivers(net)) {
            if (d->flags.has(analysis::DriverFlags::OutputPort)) { release = true; return; }
            if (d->isInputPort()) {
                const Expression* up = parentConnection(net);
                if (up && connectionCanRelease(l, *up, depth + 1)) { release = true; return; }
                continue;
            }
            if (d->kind != analysis::DriverKind::Continuous) continue;
            if (auto* ca = d->containingSymbol->as_if<ContinuousAssignSymbol>()) {
                const auto& asg = ca->getAssignment();
                if (ZSearchFn(l, asg.kind == ExpressionKind::Assignment ? asg.as<AssignmentExpression>().right() : asg)) { release = true; return; }
            } else if (auto* gate = d->containingSymbol->as_if<PrimitiveInstanceSymbol>()) {
                if (gateReleases(gate->primitiveType.name)) { release = true; return; }
            } else if (d->flags.has(analysis::DriverFlags::Initializer) && net.getInitializer() && ZSearchFn(l, *net.getInitializer())) {
                release = true;
                return;
            }
        }
    });
    return release;
}

// `copy` is a driver slang made on `target` for another instance of a module whose canonical body made `origin`. A write through
// a modport port is the origin's driver on that port: port `.p(w[1])` carries the write to bit 1 of `w`, and `output w`
// carries it to `w`. A port defined by anything but one static select of one value is not followed.
bool sameWrite(const analysis::ValueDriver& origin, const analysis::ValueDriver& copy, const ValueSymbol& target) {
    // a copy expanded from a modport port is created for the instance and has lost the procedure's kind
    if (origin.kind != copy.kind || (origin.source != copy.source && !copy.flags.has(analysis::DriverFlags::ViaIndirectPort))) return false;
    const Symbol& sym = origin.getSymbol();
    if (sym.kind != SymbolKind::ModportPort) return sym.name == target.name && origin.getBounds() == copy.getBounds();
    const auto& mp = sym.as<ModportPortSymbol>();
    if (!mp.explicitConnection) return mp.internalSymbol && mp.internalSymbol->name == target.name && origin.getBounds() == copy.getBounds();
    slang::ast::ASTContext ac(target.getParentScope()->getCompilation().getRoot(), slang::ast::LookupLocation::max);
    slang::ast::EvalContext ec(ac);
    const Expression* e = mp.explicitConnection;
    while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
    slang::ast::ValuePath path(*e, ec);
    if (path.empty() || !path.rootSymbol() || !path.isFullyStatic() || path.rootSymbol()->name != target.name) return false;
    const auto ob = origin.getBounds();
    return copy.getBounds() == std::pair<uint64_t, uint64_t>{path.lspBounds.first + ob.first, path.lspBounds.first + ob.second};
}

// The instance body a symbol sits in, through generate blocks.
const InstanceBodySymbol* enclosingBody(const Symbol& s) {
    for (const Symbol* cur = &s; cur;) {
        if (cur->kind == SymbolKind::InstanceBody) return &cur->as<InstanceBodySymbol>();
        cur = cur->getParentScope() ? &cur->getParentScope()->asSymbol() : nullptr;
    }
    return nullptr;
}

// The symbol at the same place in `to` (an instance body of the same definition) as `sym` is in its own body, found by position
// in each enclosing scope, generate blocks included. Null when there is none of the same kind and name.
const Symbol* counterpart(const Symbol& sym, const InstanceBodySymbol& to) {
    std::vector<size_t> ordinals;   // position in each enclosing scope, innermost first
    const Symbol* cur = &sym;
    while (cur->kind != SymbolKind::InstanceBody) {
        const Scope* scope = cur->getParentScope();
        if (!scope) return nullptr;
        size_t at = 0;
        bool found = false;
        for (auto& m : scope->members()) { if (&m == cur) { found = true; break; } at++; }
        if (!found) return nullptr;
        ordinals.push_back(at);
        cur = &scope->asSymbol();
    }
    const Scope* scope = &to;
    const Symbol* hit = nullptr;
    for (auto it = ordinals.rbegin(); it != ordinals.rend(); ++it) {
        if (!scope) return nullptr;
        size_t k = 0;
        hit = nullptr;
        for (auto& m : scope->members()) { if (k++ == *it) { hit = &m; break; } }
        if (!hit) return nullptr;
        scope = hit->as_if<Scope>();
    }
    return hit && hit->kind == sym.kind && hit->name == sym.name ? hit : nullptr;
}

// Whether `sym` sits in `body`, at any depth.
bool within(const Symbol* sym, const InstanceBodySymbol* body) {
    for (const Symbol* cur = sym; cur;) {
        if (cur == body) return true;
        cur = cur->getParentScope() ? &cur->getParentScope()->asSymbol() : nullptr;
    }
    return false;
}

// Whether `inner` is `outer` or sits below it in the instance hierarchy.
bool instanceWithin(const InstanceSymbol* inner, const InstanceSymbol* outer) {
    for (int hops = 0; inner && hops < 4096; hops++) {
        if (inner == outer) return true;
        const auto* body = enclosingBody(*inner);
        inner = body ? body->parentInstance : nullptr;
    }
    return false;
}

// Where a write made through an interface port by `inst` lands, found by following the instance's own port connections along the
// hierarchical path the canonical body used. Null when the path cannot be followed (a generic port that is not connected, an
// element select past the array, a name the target does not have).
const Symbol* followInterfacePath(const InstanceSymbol& inst, const HierarchicalReference& ref) {
    if (ref.path.size() < 2) return nullptr;
    const Symbol* cur = inst.body.findPort(ref.path[0].symbol->name);
    if (!cur) return nullptr;
    const ModportSymbol* modport = nullptr;
    std::optional<std::span<const Symbol* const>> elems;   // the elements of an interface array the path has narrowed to
    for (size_t i = 1; i < ref.path.size(); i++) {
        for (int hops = 0; cur && cur->kind == SymbolKind::InterfacePort; hops++) {   // a port passed down from the parent's port
            if (hops > 16) return nullptr;
            std::tie(cur, modport) = cur->as<InterfacePortSymbol>().getConnection();
        }
        if (!cur) return nullptr;
        if (!elems) {
            if (cur->kind == SymbolKind::Instance) {
                const auto& body = cur->as<InstanceSymbol>().body;
                if (body.getDefinition().definitionKind != DefinitionKind::Interface) return nullptr;
                cur = &body;
                if (modport) {
                    cur = body.find(modport->name);
                    modport = nullptr;
                    if (!cur) return nullptr;
                }
            } else if (cur->kind == SymbolKind::InstanceArray) {
                elems = cur->as<InstanceArraySymbol>().elements;
            } else if (!cur->isScope()) {
                return nullptr;
            }
        }
        const auto& step = ref.path[i];
        if (const auto* index = std::get_if<int32_t>(&step.selector)) {
            if (elems) {
                if (*index < 0 || size_t(*index) >= elems->size()) return nullptr;
                cur = (*elems)[size_t(*index)];
            } else if (cur->kind == SymbolKind::GenerateBlockArray) {
                const auto& arr = cur->as<GenerateBlockArraySymbol>();
                if (!arr.valid || *index < 0 || size_t(*index) >= arr.entries.size()) return nullptr;
                cur = arr.entries[size_t(*index)];
            } else return nullptr;
        } else if (const auto* range = std::get_if<std::pair<int32_t, int32_t>>(&step.selector)) {
            if (!elems || range->first < 0 || range->second < range->first || size_t(range->second) >= elems->size()) return nullptr;
            elems = elems->subspan(size_t(range->first), size_t(range->second - range->first) + 1);
            continue;
        } else {
            const Scope* scope = elems ? nullptr : cur->as_if<Scope>();
            if (!scope) return nullptr;
            const Symbol* next = scope->find(std::get<std::string_view>(step.selector));
            if (!next && cur->kind == SymbolKind::Modport) {   // a name the modport does not list, taken from the interface itself
                next = cur->getParentScope()->find(std::get<std::string_view>(step.selector));
                if (!next || SemanticFacts::isAllowedInModport(next->kind) || next->kind == SymbolKind::Modport) return nullptr;
            }
            cur = next;
        }
        elems.reset();
        if (!cur) return nullptr;
    }
    return cur;
}

// slang analyses only the canonical body of identical instances, so an instance inside a body that is not the
// canonical one (the second `user`, or anything below it) has no drivers on its own port nets. The instance at the
// same place in the analysed body carries them; the port connection is still read from the instance itself, so each
// instance maps the child's drivers through its own interface binding or hierarchical target.
const InstanceSymbol& analysedInstance(const InstanceSymbol& inst) {
    std::vector<size_t> ordinals;   // position of each enclosing symbol in its scope, innermost first
    const Symbol* sym = &inst;
    const InstanceBodySymbol* body = nullptr;
    while (true) {
        const Scope* scope = sym->getParentScope();
        if (!scope) return inst;
        const Symbol& owner = scope->asSymbol();
        size_t at = 0;
        bool found = false;
        for (auto& m : scope->members()) { if (&m == sym) { found = true; break; } at++; }
        if (!found) return inst;
        ordinals.push_back(at);
        if (owner.kind == SymbolKind::InstanceBody) { body = &owner.as<InstanceBodySymbol>(); break; }
        sym = &owner;
    }
    const InstanceSymbol* parent = body->parentInstance;
    if (!parent) return inst;
    const InstanceSymbol& up = analysedInstance(*parent);
    const InstanceBodySymbol* target = up.getCanonicalBody() ? up.getCanonicalBody() : &up.body;
    if (target == body) return inst;
    const Scope* scope = target;
    const Symbol* cur = nullptr;
    for (auto it = ordinals.rbegin(); it != ordinals.rend(); ++it) {
        if (!scope) return inst;
        size_t k = 0;
        cur = nullptr;
        for (auto& m : scope->members()) { if (k++ == *it) { cur = &m; break; } }
        if (!cur) return inst;
        scope = cur->as_if<Scope>();
    }
    if (cur && cur->kind == SymbolKind::Instance && &cur->as<InstanceSymbol>().getDefinition() == &inst.getDefinition()) return cur->as<InstanceSymbol>();
    return inst;
}

// A write an instance makes through an interface port. slang analyses the body of identical instances once and repeats such a
// write for another instance only where it can retarget it, and not at all inside a body it did not analyse (the second
// `wrapper`, or any instance below it). So the writes of every instance that is not the canonical one are rebuilt from the
// canonical body's own writes, followed through that instance's own connections: the origin is the canonical driver, the site
// the instance repeating it, and the bounds are those within the written net or variable.
struct PortWrite {
    const analysis::ValueDriver* origin;
    const InstanceSymbol* site;
    std::pair<uint64_t, uint64_t> bounds;
    bool undecided;   // the target is one of several (a modport port defined by an expression that is not one static select)
};
struct PortWrites {
    std::map<const Symbol*, std::vector<PortWrite>> at;                                 // by net or variable written
    std::vector<std::pair<const analysis::ValueDriver*, const InstanceSymbol*>> lost;   // writes whose target cannot be followed
    std::vector<const InstanceSymbol*> unmapped;                                        // instances whose analysed counterpart was not found
};

struct PortWriteScan : ASTVisitor<PortWriteScan, VisitFlags::AllGood> {
    analysis::AnalysisManager& am;
    PortWrites writes;
    slang::ast::ASTContext ac;
    slang::ast::EvalContext ec;
    PortWriteScan(Lint& l, analysis::AnalysisManager& a) : am(a), ac(l.compilation().getRoot(), slang::ast::LookupLocation::max), ec(ac) {}
    void note(const InstanceSymbol& inst, const analysis::ValueDriver& o, const HierarchicalReference& ref) {
        const Symbol* target = followInterfacePath(inst, ref);
        const auto ob = o.getBounds();
        auto add = [&](const Symbol* key, std::pair<uint64_t, uint64_t> bounds, bool undecided) { writes.at[key].push_back({&o, &inst, bounds, undecided}); };
        if (target && (target->kind == SymbolKind::Net || target->kind == SymbolKind::Variable)) { add(target, ob, false); return; }
        if (target && target->kind == SymbolKind::ModportPort) {
            const auto& mp = target->as<ModportPortSymbol>();
            if (mp.internalSymbol) { add(mp.internalSymbol, ob, false); return; }
            if (mp.explicitConnection) {   // `.p(w[1])`: the write is to the bits of that expression
                const Expression* e = mp.explicitConnection;
                while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
                slang::ast::ValuePath path(*e, ec);
                if (!path.empty() && path.rootSymbol() && path.isFullyStatic()) {
                    add(path.rootSymbol(), {path.lspBounds.first + ob.first, path.lspBounds.first + ob.second}, false);
                    return;
                }
                bool any = false;
                mp.explicitConnection->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                    if (sym.kind == SymbolKind::Net || sym.kind == SymbolKind::Variable) { add(&sym, ob, true); any = true; }
                });
                if (any) return;
            }
        }
        writes.lost.push_back({&o, &inst});
    }
    // Whether slang analysed the body of `inst` itself: it is not a copy of another instance's body and neither is anything above it.
    static bool analysed(const InstanceSymbol& inst) {
        if (inst.getCanonicalBody()) return false;
        const auto* body = enclosingBody(inst);
        return !body || !body->parentInstance || analysed(*body->parentInstance);
    }
    void handle(const InstanceSymbol& inst) {
        if (!analysed(inst)) {
            // the instance whose body slang did analyse, at the same place in the canonical copy of the enclosing bodies
            const InstanceSymbol& twin = analysedInstance(inst);
            const InstanceBodySymbol* canon = twin.getCanonicalBody() ? twin.getCanonicalBody() : &twin.body;
            if (&twin == &inst && !inst.getCanonicalBody()) writes.unmapped.push_back(&inst);
            else if (auto state = am.getInstanceDriverState(*canon))
                for (auto& hp : state->hierPortDrivers) {
                    if (!hp.ref || !hp.ref->isViaIfacePort()) continue;
                    const auto* o = hp.driver.get();
                    // a write made by a child instance is repeated by the child itself; this one is the instance's own
                    if (enclosingBody(*o->containingSymbol) != canon) continue;
                    // an instance output driving the port is not modelled (as for the drivers slang attributes directly)
                    if (o->flags.has(analysis::DriverFlags::OutputPort)) continue;
                    note(inst, *o, *hp.ref);
                }
        }
        visitDefault(inst);
    }
};

// One driver of a net or variable as the rule sees it.
struct DriverRef {
    const analysis::ValueDriver* d;       // slang's driver; for a port write, the canonical body's driver the site repeats
    std::pair<uint64_t, uint64_t> bounds;
    const InstanceSymbol* site = nullptr; // set when `d` is repeated by this instance through an interface port
    const Symbol* where = nullptr;        // what an unconfirmed driver is attributed to
    bool undecided = false;               // the attribution through identical instances cannot be confirmed: counts, never an ERROR
};

// Whether slang made `copy` (a driver for another instance) from a `ref` port write: those are the only copies it makes that are
// not rebuilt by PortWriteScan.
bool refPortCopy(analysis::AnalysisManager& am, const InstanceSymbol& inst, const analysis::ValueDriver& copy, const ValueSymbol& target) {
    const InstanceBodySymbol* body = inst.getCanonicalBody();
    if (!body) body = &inst.body;
    if (auto state = am.getInstanceDriverState(*body))
        for (auto& hp : state->hierPortDrivers)
            if (!hp.ref && sameWrite(*hp.driver.get(), copy, target)) return true;
    return false;
}

// The drivers of a net or variable. slang analyses the body of an interface instance once, so the net of a second identical
// instance has the drivers modules make through ports but not the interface's own assignments; those are taken from the
// canonical instance's counterpart, found by position (a net in a generate block included). Writes made through interface ports
// by instances slang did not analyse come from `pw`; a copy slang made that `pw` does not account for, and a counterpart that
// cannot be found, stay as unconfirmed drivers.
std::vector<DriverRef> driversOf(analysis::AnalysisManager& am, const PortWrites& pw, const ValueSymbol& v) {
    std::vector<DriverRef> out;
    const auto whole = [&] { const uint64_t w = v.getType().getSelectableWidth(); return std::pair<uint64_t, uint64_t>{0, w ? w - 1 : 0}; };
    const auto rebuilt = pw.at.find(&v);
    for (auto* d : am.getDrivers(v)) {
        const auto* inst = d->containingSymbol->as_if<InstanceSymbol>();
        if (inst && d->flags.has(analysis::DriverFlags::FromSideEffect) && !refPortCopy(am, *inst, *d, v)) {
            bool covered = false;
            if (rebuilt != pw.at.end())
                for (auto& w : rebuilt->second) covered = covered || instanceWithin(w.site, inst);
            if (!covered) out.push_back({d, d->getBounds(), nullptr, inst, true});
            continue;
        }
        out.push_back({d, d->getBounds()});
    }
    if (rebuilt != pw.at.end())
        for (auto& w : rebuilt->second) out.push_back({w.origin, w.bounds, w.site, w.site, w.undecided});
    const auto* body = enclosingBody(v);
    if (!body || !body->parentInstance || body->getDefinition().definitionKind != DefinitionKind::Interface) return out;
    const auto* canon = body->parentInstance->getCanonicalBody();
    if (!canon || canon == body) return out;
    const Symbol* twin = counterpart(v, *canon);
    if (!twin) {
        out.push_back({nullptr, whole(), nullptr, &v, true});
        return out;
    }
    for (auto* d : am.getDrivers(twin->as<ValueSymbol>())) {
        if (d->flags.has(analysis::DriverFlags::FromSideEffect)) continue;
        if (within(d->containingSymbol, canon)) out.push_back({d, d->getBounds()});
    }
    return out;
}

class NetConflictRule final : public LintRule {
public:
    NetConflictRule() : LintRule("LNT-009", "", true) {}
    // The plain always procedures that call each subroutine, directly or through other subroutines: a write inside a task
    // is made by every procedure that calls it.
    using Callers = std::map<const SubroutineSymbol*, std::set<const ProceduralBlockSymbol*>>;
    static Callers callersOf(Lint& l) {
        struct C : ASTVisitor<C, VisitFlags::AllGood> {
            std::map<const SubroutineSymbol*, std::set<const ProceduralBlockSymbol*>> direct;
            std::map<const SubroutineSymbol*, std::set<const SubroutineSymbol*>> nested;   // callee -> calling subroutines
            const ProceduralBlockSymbol* block = nullptr;
            const SubroutineSymbol* routine = nullptr;
            void handle(const ProceduralBlockSymbol& b) { auto outer = block; block = &b; visitDefault(b); block = outer; }
            void handle(const SubroutineSymbol& r) { auto outer = routine; auto ob = block; routine = &r; block = nullptr; visitDefault(r); routine = outer; block = ob; }
            void handle(const CallExpression& call) {
                if (!call.isSystemCall())
                    if (const auto* callee = std::get<0>(call.subroutine)) {
                        if (routine) nested[callee].insert(routine);
                        else if (block && block->procedureKind == ProceduralBlockKind::Always) direct[callee].insert(block);
                    }
                visitDefault(call);
            }
        } c;
        l.visitHardware(c);
        Callers all;
        for (auto& [callee, _] : c.nested) all[callee];
        for (auto& [callee, _] : c.direct) all[callee];
        for (auto& [callee, blocks] : all) {   // close over subroutines calling subroutines
            std::set<const SubroutineSymbol*> seen{callee};
            std::vector<const SubroutineSymbol*> pending{callee};
            while (!pending.empty()) {
                auto* r = pending.back(); pending.pop_back();
                if (auto it = c.direct.find(r); it != c.direct.end()) blocks.insert(it->second.begin(), it->second.end());
                if (auto it = c.nested.find(r); it != c.nested.end())
                    for (auto* up : it->second) if (seen.insert(up).second) pending.push_back(up);
            }
        }
        return all;
    }
    // Loop counters: module-level variables that the code itself shows to be per-loop temporaries. EVERY write to the
    // variable is an initialiser or step of a `for` loop whose initialiser writes it, and EVERY read is inside the
    // condition, step or body of such a loop in the same procedure. One write or read anywhere else (a statement after the
    // loop, a body write, a continuous assignment, an event control, a subroutine, a port or hierarchical reference)
    // makes the variable state that other procedures can see. slang's drivers are not used: they keep one write per
    // procedure and range, so they cannot tell a counter from a variable that also holds a value.
    struct CounterScan : ASTVisitor<CounterScan, VisitFlags::AllGood> {
        struct Frame { std::set<const Symbol*> inits; bool looping = false; };
        std::set<const Symbol*> initialised, notCounter;
        std::vector<Frame> frames;
        const Frame* control = nullptr;   // the loop whose initialisers or steps are being visited
        void write(const Symbol* sym) {
            if (control && control->inits.count(sym)) initialised.insert(sym);
            else notCounter.insert(sym);
        }
        void read(const Symbol* sym) {
            for (auto& f : frames)
                if (f.looping && f.inits.count(sym)) return;
            notCounter.insert(sym);
        }
        // The variables an assignment target writes (an index inside the target is only read). Any other shape: every name in it.
        void roots(const Expression& e, std::vector<const Symbol*>& out) {
            switch (e.kind) {
                case ExpressionKind::NamedValue: out.push_back(&e.as<NamedValueExpression>().symbol); return;
                case ExpressionKind::HierarchicalValue: out.push_back(&e.as<HierarchicalValueExpression>().symbol); return;
                case ExpressionKind::ElementSelect: roots(e.as<ElementSelectExpression>().value(), out); return;
                case ExpressionKind::RangeSelect: roots(e.as<RangeSelectExpression>().value(), out); return;
                case ExpressionKind::MemberAccess: roots(e.as<MemberAccessExpression>().value(), out); return;
                case ExpressionKind::Conversion: roots(e.as<ConversionExpression>().operand(), out); return;
                case ExpressionKind::Concatenation:
                    for (auto* op : e.as<ConcatenationExpression>().operands()) roots(*op, out);
                    return;
                default: e.visitSymbolReferences([&](const Expression&, const Symbol& sym) { out.push_back(&sym); });
            }
        }
        // The parts of a target that are reads: indexes and bounds.
        void targetReads(const Expression& e) {
            switch (e.kind) {
                case ExpressionKind::NamedValue: case ExpressionKind::HierarchicalValue: return;
                case ExpressionKind::ElementSelect:
                    targetReads(e.as<ElementSelectExpression>().value());
                    e.as<ElementSelectExpression>().selector().visit(*this);
                    return;
                case ExpressionKind::RangeSelect:
                    targetReads(e.as<RangeSelectExpression>().value());
                    e.as<RangeSelectExpression>().left().visit(*this);
                    e.as<RangeSelectExpression>().right().visit(*this);
                    return;
                case ExpressionKind::MemberAccess: targetReads(e.as<MemberAccessExpression>().value()); return;
                case ExpressionKind::Conversion: targetReads(e.as<ConversionExpression>().operand()); return;
                case ExpressionKind::Concatenation:
                    for (auto* op : e.as<ConcatenationExpression>().operands()) targetReads(*op);
                    return;
                default: e.visit(*this);
            }
        }
        void target(const Expression& e, bool alsoRead) {
            std::vector<const Symbol*> written;
            roots(e, written);
            for (auto* sym : written) { write(sym); if (alsoRead) read(sym); }
            targetReads(e);
        }
        void handle(const NamedValueExpression& e) { read(&e.symbol); }
        void handle(const HierarchicalValueExpression& e) { notCounter.insert(&e.symbol); }
        void handle(const AssignmentExpression& a) { target(a.left(), a.isCompound()); a.right().visit(*this); }
        void handle(const UnaryExpression& u) {
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement ||
                u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Postdecrement) target(u.operand(), true);
            else visitDefault(u);
        }
        void handle(const CallExpression& c) {
            // an inout or ref argument may be written by the callee
            if (!c.isSystemCall())
                if (const auto* sub = std::get<0>(c.subroutine)) {
                    auto formals = sub->getArguments();
                    for (size_t i = 0; i < formals.size() && i < c.arguments().size(); i++)
                        if (formals[i]->direction == ArgumentDirection::InOut || formals[i]->direction == ArgumentDirection::Ref) {
                            std::vector<const Symbol*> hit;
                            roots(*c.arguments()[i], hit);
                            notCounter.insert(hit.begin(), hit.end());
                        }
                }
            visitDefault(c);
        }
        void handle(const InstanceSymbol& inst) {
            for (auto* pc : inst.getPortConnections())
                if (const Expression* ex = pc->getExpression()) ex->visit(*this);
            visitDefault(inst);
        }
        void handle(const PrimitiveInstanceSymbol& prim) {
            for (auto* ex : prim.getPortConnections()) {
                std::vector<const Symbol*> hit;
                roots(*ex, hit);   // a gate terminal may be driven: no loop counter appears there
                notCounter.insert(hit.begin(), hit.end());
            }
        }
        template<typename T> void isolate(const T& sym) {   // a procedure or subroutine starts with no enclosing loop
            auto outer = std::move(frames);
            auto* ctl = control;
            frames.clear();
            control = nullptr;
            visitDefault(sym);
            frames = std::move(outer);
            control = ctl;
        }
        void handle(const ProceduralBlockSymbol& b) { isolate(b); }
        void handle(const SubroutineSymbol& r) { isolate(r); }
        void handle(const ForLoopStatement& f) {
            frames.push_back({});
            for (auto* e : f.initializers)
                if (e->kind == ExpressionKind::Assignment) {
                    std::vector<const Symbol*> hit;
                    roots(e->as<AssignmentExpression>().left(), hit);
                    frames.back().inits.insert(hit.begin(), hit.end());
                }
            const size_t me = frames.size() - 1;
            auto* outer = control;
            control = &frames[me];
            for (auto* e : f.initializers) e->visit(*this);
            frames[me].looping = true;
            control = nullptr;
            if (f.stopExpr) f.stopExpr->visit(*this);
            control = &frames[me];
            for (auto* e : f.steps) e->visit(*this);
            control = nullptr;
            f.body.visit(*this);
            control = outer;
            frames.pop_back();
        }
        bool isCounter(const Symbol* sym) const { return initialised.count(sym) && !notCounter.count(sym); }
    };
    // Writes whose element or bit is chosen at run time. slang's driver for such a write is the whole variable, so the
    // assignments themselves say which writers are dynamic. An index made of block-local values or of a loop counter is
    // a loop's own and slang unrolls it exactly, so only a select that reads other module-level state counts.
    struct RuntimeIndexScan : ASTVisitor<RuntimeIndexScan, VisitFlags::AllGood> {
        Lint& l;
        const CounterScan& counters;
        slang::ast::ASTContext ac;
        slang::ast::EvalContext ec;
        std::set<std::pair<const Symbol*, const Symbol*>> dynamic;   // (procedure or subroutine, written variable)
        std::set<std::pair<const Symbol*, const Symbol*>> indexDynamic;   // ... of which an index chosen at run time (or an unknown loop range) is the reason; the others are loops left early
        int guardDepth = 0;   // conditions, case arms and loops around the node being visited
        void handle(const GenerateBlockSymbol& g) { if (!g.isUninstantiated) visitDefault(g); }
        void handle(const InstanceBodySymbol& body) {
            if (!body.flags.has(InstanceFlags::Uninstantiated)) visitDefault(body);
        }
        // The bits of a packed variable that run-time selects of one writer can reach (inclusive intervals of bit offsets), when
        // the selector is `a*x + b` of one narrow variable x: every value of x is tried. Anything else is opaque.
        using Iv = std::pair<uint64_t, uint64_t>;
        struct Reach { bool opaque = false; std::vector<Iv> iv; };
        std::map<std::pair<const Symbol*, const Symbol*>, Reach> reach;
        static int64_t wrap(int64_t v, const Type& t, bool& ok) {
            const auto w = t.getBitWidth();
            if (!t.isIntegral() || w == 0 || w > 62) { ok = false; return 0; }
            v &= (int64_t{1} << w) - 1;
            if (t.isSigned() && (v >> (w - 1)) & 1) v -= int64_t{1} << w;
            return v;
        }
        int64_t evalAt(const Expression& e, const Symbol* x, int64_t xv, bool& ok) const {
            if (!ok) return 0;
            switch (e.kind) {
                case ExpressionKind::NamedValue:
                    if (&e.as<NamedValueExpression>().symbol == x) return xv;
                    break;
                case ExpressionKind::Conversion: {
                    // IEEE 1800 11.8.2: the operand is extended by the signedness of the expression it sits in, so a signed
                    // operand converted to an unsigned type is zero-extended from its own width.
                    const auto& op = e.as<ConversionExpression>().operand();
                    int64_t v = evalAt(op, x, xv, ok);
                    const auto ow = op.type->getBitWidth();
                    if (ok && op.type->isIntegral() && op.type->isSigned() && !e.type->isSigned() && ow > 0 && ow < 63) v &= (int64_t{1} << ow) - 1;
                    return wrap(v, *e.type, ok);
                }
                case ExpressionKind::BinaryOp: {
                    const auto& b = e.as<BinaryExpression>();
                    const int64_t lv = evalAt(b.left(), x, xv, ok), rv = evalAt(b.right(), x, xv, ok);
                    switch (b.op) {
                        case BinaryOperator::Add: return wrap(lv + rv, *e.type, ok);
                        case BinaryOperator::Subtract: return wrap(lv - rv, *e.type, ok);
                        case BinaryOperator::Multiply: return wrap(lv * rv, *e.type, ok);
                        case BinaryOperator::Divide: case BinaryOperator::Mod:
                            if (rv == 0) { ok = false; return 0; }
                            return wrap(b.op == BinaryOperator::Divide ? lv / rv : lv % rv, *e.type, ok);
                        case BinaryOperator::LogicalShiftLeft: case BinaryOperator::ArithmeticShiftLeft:
                            if (rv < 0 || rv > 30) { ok = false; return 0; }
                            return wrap(lv << rv, *e.type, ok);
                        case BinaryOperator::LogicalShiftRight: case BinaryOperator::ArithmeticShiftRight: {
                            const auto w = e.type->getBitWidth();
                            if (rv < 0 || rv > 30 || !e.type->isIntegral() || w == 0 || w > 62) { ok = false; return 0; }
                            // `>>>` keeps the sign only in a signed expression; `>>` shifts in zeros
                            if (b.op == BinaryOperator::ArithmeticShiftRight && e.type->isSigned()) return wrap(lv >> rv, *e.type, ok);
                            return wrap(static_cast<int64_t>((static_cast<uint64_t>(lv) & ((uint64_t{1} << w) - 1)) >> rv), *e.type, ok);
                        }
                        default: ok = false; return 0;
                    }
                }
                case ExpressionKind::Call: {   // $signed / $unsigned reinterpret their operand's bits
                    const auto& c = e.as<CallExpression>();
                    if (c.isSystemCall() && c.arguments().size() == 1 && (c.getSubroutineName() == "$signed" || c.getSubroutineName() == "$unsigned"))
                        return wrap(evalAt(*c.arguments()[0], x, xv, ok), *e.type, ok);
                    break;
                }
                case ExpressionKind::Concatenation: {   // the operands' bits, first operand highest
                    uint64_t bits = 0;
                    uint64_t total = 0;
                    for (auto* op : e.as<ConcatenationExpression>().operands()) {
                        const auto w = op->type->getBitWidth();
                        if (!op->type->isIntegral() || w == 0 || total + w > 62) { ok = false; return 0; }
                        const int64_t v = evalAt(*op, x, xv, ok);
                        bits = (bits << w) | (static_cast<uint64_t>(v) & ((uint64_t{1} << w) - 1));
                        total += w;
                    }
                    return ok ? wrap(static_cast<int64_t>(bits), *e.type, ok) : 0;
                }
                case ExpressionKind::RangeSelect: {   // a constant part-select of an integral value
                    const auto& r = e.as<RangeSelectExpression>();
                    if (r.getSelectionKind() != slang::ast::RangeSelectionKind::Simple) break;
                    auto hi = l.evalConst(r.left()), lo = l.evalConst(r.right());
                    const Type& vt = *r.value().type;
                    if (!hi.isInteger() || !lo.isInteger() || hi.integer().hasUnknown() || lo.integer().hasUnknown() ||
                        !vt.isIntegral() || vt.getBitWidth() > 62 || !vt.hasFixedRange()) break;
                    auto h = hi.integer().as<int32_t>(), q = lo.integer().as<int32_t>();
                    if (!h || !q) break;
                    const auto fr = vt.getFixedRange();
                    if (!fr.containsPoint(*h) || !fr.containsPoint(*q)) { ok = false; return 0; }
                    const int64_t a = fr.translateIndex(*h), c = fr.translateIndex(*q);
                    const int64_t lowBit = std::min(a, c), width = std::abs(a - c) + 1;
                    const uint64_t raw = static_cast<uint64_t>(evalAt(r.value(), x, xv, ok)) >> lowBit;
                    return ok ? wrap(static_cast<int64_t>(raw & ((uint64_t{1} << width) - 1)), *e.type, ok) : 0;
                }
                case ExpressionKind::ElementSelect: {   // a constant element (a bit) of a packed value
                    const auto& es = e.as<ElementSelectExpression>();
                    auto idx = l.evalConst(es.selector());
                    const Type& vt = *es.value().type;
                    if (!idx.isInteger() || idx.integer().hasUnknown() || !vt.isIntegral() || vt.getBitWidth() > 62 || !vt.hasFixedRange() || !e.type->isIntegral()) break;
                    auto at = idx.integer().as<int32_t>();
                    if (!at) break;
                    const auto fr = vt.getFixedRange();
                    if (!fr.containsPoint(*at) || fr.fullWidth() == 0) { ok = false; return 0; }
                    const auto w = e.type->getBitWidth();
                    if (w == 0 || w * fr.fullWidth() != vt.getBitWidth()) break;
                    const uint64_t raw = static_cast<uint64_t>(evalAt(es.value(), x, xv, ok)) >> (static_cast<uint64_t>(fr.translateIndex(*at)) * w);
                    return ok ? wrap(static_cast<int64_t>(raw & ((uint64_t{1} << w) - 1)), *e.type, ok) : 0;
                }
                case ExpressionKind::MemberAccess: {   // a field of a packed struct
                    const auto& m = e.as<MemberAccessExpression>();
                    const auto* f = m.member.as_if<slang::ast::FieldSymbol>();
                    const Type& st = *m.value().type;
                    if (!f || st.getCanonicalType().kind != SymbolKind::PackedStructType || st.getBitWidth() > 62 || !e.type->isIntegral()) break;
                    const uint64_t raw = static_cast<uint64_t>(evalAt(m.value(), x, xv, ok)) >> f->bitOffset;
                    const auto w = e.type->getBitWidth();
                    return ok ? wrap(static_cast<int64_t>(raw & ((uint64_t{1} << w) - 1)), *e.type, ok) : 0;
                }
                default: break;
            }
            auto cv = l.evalConst(e);
            if (!cv.isInteger() || cv.integer().hasUnknown() || cv.integer().getBitWidth() > 62) { ok = false; return 0; }
            auto v = cv.integer().as<int64_t>();
            if (!v) { ok = false; return 0; }
            return *v;
        }
        // `counter`: the selector reads a loop counter (and nothing else), so the values it takes are the loop's own, not every
        // value of its type.
        Reach computeReach(const Symbol* var, const Expression& full, const lint::LoopValues* counter) {
            Reach r;
            const Expression* sel = nullptr;
            const Expression* base = nullptr;
            const RangeSelectExpression* range = nullptr;
            if (full.kind == ExpressionKind::ElementSelect) { sel = &full.as<ElementSelectExpression>().selector(); base = &full.as<ElementSelectExpression>().value(); }
            else if (full.kind == ExpressionKind::RangeSelect && full.as<RangeSelectExpression>().getSelectionKind() != slang::ast::RangeSelectionKind::Simple) {
                range = &full.as<RangeSelectExpression>();
                sel = &range->left();
                base = &range->value();
            }
            std::vector<Iv> out;
            do {
                if (!sel || !base || base->kind != ExpressionKind::NamedValue || &base->as<NamedValueExpression>().symbol != var) { r.opaque = true; break; }
                const Type& vt = var->as<ValueSymbol>().getType();
                const bool memory = vt.isUnpackedArray() && vt.hasFixedRange();
                if (!(vt.isIntegral() && !vt.isScalar()) && !(counter && memory)) { r.opaque = true; break; }
                std::set<const Symbol*> vars;
                sel->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                    if (sym.kind != SymbolKind::Parameter && sym.kind != SymbolKind::EnumValue && sym.kind != SymbolKind::Field) vars.insert(&sym);
                });
                if (vars.size() != 1 || !(*vars.begin())->as_if<ValueSymbol>()) { r.opaque = true; break; }
                const Symbol* x = *vars.begin();
                const Type& xt = x->as<ValueSymbol>().getType();
                if (counter ? counter->var != x || counter->count > 4096
                            : !xt.isIntegral() || xt.getBitWidth() == 0 || xt.getBitWidth() > 12) { r.opaque = true; break; }
                int64_t span = 1;
                if (range) {
                    auto w = l.evalConst(range->right());
                    if (!w.isInteger() || w.integer().hasUnknown() || !w.integer().as<int32_t>() || *w.integer().as<int32_t>() < 1) { r.opaque = true; break; }
                    span = *w.integer().as<int32_t>();
                }
                const slang::ConstantRange fr = vt.getFixedRange();
                const uint64_t unit = fr.fullWidth() ? (memory ? vt.getArrayElementType()->getSelectableWidth() : vt.getBitWidth() / fr.fullWidth()) : 1;   // bits per index position
                bool ok = true;
                const uint64_t n = counter ? counter->count : uint64_t{1} << xt.getBitWidth();
                for (uint64_t u = 0; u < n && ok; u++) {
                    const int64_t xv = counter ? counter->at(u) : wrap(static_cast<int64_t>(u), xt, ok);
                    const int64_t idx = evalAt(*sel, x, xv, ok);
                    if (!ok) break;
                    slang::ConstantRange sub;
                    if (range) {
                        auto q = slang::ConstantRange::getIndexedRange(static_cast<int32_t>(idx), static_cast<int32_t>(span), fr.isDescending(),
                                                                       range->getSelectionKind() == slang::ast::RangeSelectionKind::IndexedUp);
                        if (!q || !q->overlaps(fr)) continue;
                        sub = q->intersect(fr);
                    } else {
                        if (idx < INT32_MIN || idx > INT32_MAX || !fr.containsPoint(static_cast<int32_t>(idx))) continue;
                        sub = {static_cast<int32_t>(idx), static_cast<int32_t>(idx)};
                    }
                    // slang's bounds of an unpacked element are its index times the element width, not a translated offset
                    const auto a = memory ? sub.lower() : fr.translateIndex(sub.lower()), b = memory ? sub.upper() : fr.translateIndex(sub.upper());
                    out.push_back({static_cast<uint64_t>(std::min(a, b)) * unit, (static_cast<uint64_t>(std::max(a, b)) + 1) * unit - 1});
                }
                if (!ok) r.opaque = true;
            } while (false);
            r.iv = std::move(out);
            return r;
        }
        void noteReach(const Symbol* var, const Expression& full) {
            Reach r = computeReach(var, full, nullptr);
            auto& slot = reach[{container, var}];
            if (r.opaque) slot.opaque = true;
            slot.iv.insert(slot.iv.end(), r.iv.begin(), r.iv.end());
        }
        const Symbol* container = nullptr;
        RuntimeIndexScan(Lint& lint, const CounterScan& c)
            : l(lint), counters(c), ac(lint.compilation().getRoot(), slang::ast::LookupLocation::max), ec(ac) {}
        // The writes of one procedure or subroutine to one variable, kept when it writes through a loop counter (slang's driver
        // for such a write is the whole variable and absorbs the procedure's other writes): `fixed` are bit ranges written
        // for certain (static selects and the loop's own values), `dyn` the ranges of writes whose bits are chosen at run time.
        // `wide`: the bits written under a loop whose `continue` runs in some iteration; slang drops every iteration after it, so its
        // driver bounds for those writes stop short and are no evidence of what the loop does not reach.
        struct Writes { bool counter = false; std::vector<Iv> fixed, dyn, certain, wide; };   // certain: static writes no condition or loop guards
        std::map<std::pair<const Symbol*, const Symbol*>, Writes> writes;
        // Every assignment target in a procedure or subroutine body, read from the syntax: slang's driver list drops writes after a
        // decided `continue` and in loops it takes for never executing, so this is the floor under it. A write slang lists no driver
        // for, or a narrower one than the select names, is a possible driver all the same.
        struct FloorWrite {
            Iv bounds;                // the longest static prefix of the target: a superset of the bits it names
            bool isStatic = false;    // the target names exactly those bits
            bool certain = false;     // no condition, loop or exit guards it
            bool exitOnly = false;    // inside a loop a certain break (not a continue) ends: slang's drivers are exact there
        };
        std::map<std::pair<const Symbol*, const Symbol*>, std::vector<FloorWrite>> floor;
        std::map<const Symbol*, std::vector<const Symbol*>> floorContainers;   // variable -> containers with floor writes
        int deadDepth = 0;   // branches a constant condition excludes, and statements after a certain break, continue, return or disable
        std::vector<const Symbol*> blocks;   // the named blocks around the statement being visited
        void handle(const BlockStatement& b) {
            if (b.blockSymbol) blocks.push_back(b.blockSymbol);
            visitDefault(b);
            if (b.blockSymbol) blocks.pop_back();
        }
        void handle(const StatementList& s) {
            int dead = 0;
            for (auto* c : s.list) {
                c->visit(*this);
                if (!dead && lint::alwaysJumps(*c, blocks)) { ++deadDepth; dead = 1; }
            }
            deadDepth -= dead;
        }
        // The bits the writes of one procedure reach in the iterations that can run, in loops left early by exits that can all be judged
        // (loop_facts.h exitProfile): slang's drivers are exact for those iterations, so they are checked against this prediction.
        struct LiveBits { bool opaque = false; std::vector<Iv> bits; };
        std::map<std::pair<const Symbol*, const Symbol*>, LiveBits> live;
        std::vector<std::optional<lint::ExitProfile>> profiles;   // one per enclosing early loop
        std::map<const Symbol*, lint::LoopValues> loops;   // the loops being visited whose counter values are known
        struct Refs : ASTVisitor<Refs, VisitFlags::AllGood> {
            const CounterScan& counters;
            const Symbol* root;
            bool runtime = false;
            std::set<const Symbol*> counter;
            Refs(const CounterScan& c, const Symbol* r) : counters(c), root(r) {}
            void check(const Symbol& sym) {
                if (&sym == root || !isModuleLevel(sym) || sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue) return;   // constants
                if (counters.isCounter(&sym)) counter.insert(&sym);
                else runtime = true;
            }
            void handle(const NamedValueExpression& e) { check(e.symbol); }
            void handle(const HierarchicalValueExpression& e) { runtime = true; (void)e; }
        };
        // The bits a write in an early loop can reach in the iterations that run, from the loop's exit profile; opaque when not predictable.
        void notePrediction(const slang::ast::ValuePath& p) {
            auto& lv = live[{container, p.rootSymbol()}];
            if (lv.opaque) return;
            if (earlyLoops != 1 || profiles.size() != 1 || !profiles[0]) { lv.opaque = true; return; }
            const auto& pr = *profiles[0];
            bool foreign = false;
            p.fullExpr->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                if (sym.kind == SymbolKind::Parameter || sym.kind == SymbolKind::EnumValue || sym.kind == SymbolKind::Field || &sym == p.rootSymbol()) return;
                if (&sym != pr.values.var) foreign = true;
            });
            if (foreign) { lv.opaque = true; return; }
            uint64_t work = 0;
            for (size_t c = 0; c < pr.combos.size() && !lv.opaque; c++) {
                auto liveIters = pr.live(c, p.fullExpr->sourceRange.start());
                if (!liveIters) { lv.opaque = true; break; }   // the order of the exit and the write is not known
                for (uint64_t k : *liveIters) {
                    if (++work > 16384) { lv.opaque = true; break; }
                    lint::LoopValues one = pr.values;
                    one.first = pr.values.at(k);
                    one.count = 1;
                    Reach r = computeReach(p.rootSymbol(), *p.fullExpr, &one);
                    if (r.opaque) { lv.opaque = true; break; }
                    lv.bits.insert(lv.bits.end(), r.iv.begin(), r.iv.end());
                }
            }
        }
        void note(const Expression& lhs) {
            if (!container) return;
            slang::ast::ValuePath::visitPaths(lhs, ec, [&](const slang::ast::ValuePath& p) {
                if (p.empty() || !p.rootSymbol()) return;
                auto noteFloor = [&](Iv bounds, bool isStatic) {
                    if (deadDepth != 0) return;
                    auto& fl = floor[{container, p.rootSymbol()}];
                    if (fl.empty()) floorContainers[p.rootSymbol()].push_back(container);
                    fl.push_back({bounds, isStatic, isStatic && guardDepth == 0, earlyLoops != 0 && continueLoops == 0 && unjudgedLoops == 0});
                };
                auto& w = writes[{container, p.rootSymbol()}];
                const size_t wideBefore = w.wide.size();
                if (p.isFullyStatic()) {
                    noteFloor(p.lspBounds, true);
                    w.fixed.push_back(p.lspBounds);
                    if (guardDepth == 0) w.certain.push_back(p.lspBounds);
                    return;
                }
                // a write under a loop with a `continue` that runs names the bits of every iteration (wide); otherwise the select's longest static prefix
                struct FloorOnExit { std::function<void()> fn; ~FloorOnExit() { fn(); } } floorOnExit{[&] {
                    if (w.wide.size() > wideBefore) for (size_t i = wideBefore; i < w.wide.size(); i++) noteFloor(w.wide[i], false);
                    else noteFloor(p.lspBounds, false);
                }};
                Refs refs(counters, p.rootSymbol());
                p.fullExpr->visit(refs);
                if (refs.runtime) {
                    dynamic.insert({container, p.rootSymbol()});
                    indexDynamic.insert({container, p.rootSymbol()});
                    noteReach(p.rootSymbol(), *p.fullExpr);
                    w.dyn.push_back(p.lspBounds);
                } else if (!refs.counter.empty()) {
                    // an index made of a loop counter: the loop's own values reach the bits, when the loop gives them
                    w.counter = true;
                    Reach r;
                    if (refs.counter.size() == 1)
                        if (auto it = loops.find(*refs.counter.begin()); it != loops.end()) r = computeReach(p.rootSymbol(), *p.fullExpr, &it->second);
                        else r.opaque = true;
                    else r.opaque = true;
                    if (!r.opaque && earlyLoops == 0) w.fixed.insert(w.fixed.end(), r.iv.begin(), r.iv.end());
                    else if (!r.opaque) {   // a loop left early (a break, a call, a disable): the iterations that run are not known
                        dynamic.insert({container, p.rootSymbol()});
                        reach[{container, p.rootSymbol()}].opaque = true;
                        w.dyn.push_back(p.lspBounds);
                    }
                    else {   // the loop's range is not known: the write is undecided
                        dynamic.insert({container, p.rootSymbol()});
                        indexDynamic.insert({container, p.rootSymbol()});
                        reach[{container, p.rootSymbol()}].opaque = true;
                        w.dyn.push_back(p.lspBounds);
                    }
                } else if (earlyLoops != 0) {
                    // block-local values of a loop that a break, call or disable may leave early: slang's unrolled drivers
                    // name every iteration, but the writes that run are not known; the bits they reach are a guess, never a proof
                    notePrediction(p);
                    dynamic.insert({container, p.rootSymbol()});
                    reach[{container, p.rootSymbol()}].opaque = true;
                    w.dyn.push_back(p.lspBounds);
                    if (continueLoops != 0) {
                        // every iteration's bits, as if no `continue` ran: a superset of what the loop writes
                        Reach every;
                        every.opaque = true;
                        const lint::LoopValues* counter = nullptr;
                        int mentioned = 0;
                        p.fullExpr->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                            if (auto it = loops.find(&sym); it != loops.end()) { counter = &it->second; ++mentioned; }
                        });
                        if (counter && mentioned == 1) every = computeReach(p.rootSymbol(), *p.fullExpr, counter);
                        if (every.opaque) w.wide.push_back(p.lspBounds);
                        else w.wide.insert(w.wide.end(), every.iv.begin(), every.iv.end());
                    }
                } else w.fixed.push_back(p.lspBounds);   // block-local values slang unrolls
            }, /* skipSelectors */ true);
        }
        template<typename T> void guarded(const T& s) { ++guardDepth; visitDefault(s); --guardDepth; }
        template<typename T> void runtimeLoop(const T& s) { ++unjudgedLoops; guarded(s); --unjudgedLoops; }
        // A branch a constant condition excludes is not hardware: its writes are no possible drivers.
        void handle(const ConditionalStatement& s) {
            std::optional<bool> constant;
            if (s.conditions.size() == 1 && !s.conditions[0].pattern) {
                auto cv = l.evalConst(*s.conditions[0].expr);
                if (cv.isInteger() && !cv.integer().hasUnknown()) constant = !bool(cv.integer() == 0);
            }
            ++guardDepth;
            for (const auto& c : s.conditions) {
                c.expr->visit(*this);
                if (c.pattern) c.pattern->visit(*this);
            }
            deadDepth += constant && !*constant;
            s.ifTrue.visit(*this);
            deadDepth -= constant && !*constant;
            if (s.ifFalse) {
                deadDepth += constant && *constant;
                s.ifFalse->visit(*this);
                deadDepth -= constant && *constant;
            }
            --guardDepth;
        }
        void handle(const CaseStatement& s) {
            auto [taken, known] = s.getKnownBranch(ec);   // arms a constant selector excludes are not hardware
            if (!known) { guarded(s); return; }
            s.expr.visit(*this);
            ++guardDepth;
            for (const auto& item : s.items) {
                for (auto* e : item.expressions) e->visit(*this);
                deadDepth += item.stmt.get() != taken;
                item.stmt->visit(*this);
                deadDepth -= item.stmt.get() != taken;
            }
            if (s.defaultCase) {
                deadDepth += s.defaultCase != taken;
                s.defaultCase->visit(*this);
                deadDepth -= s.defaultCase != taken;
            }
            --guardDepth;
        }
        void handle(const PatternCaseStatement& s) { guarded(s); }
        void handle(const WhileLoopStatement& s) { runtimeLoop(s); }
        void handle(const DoWhileLoopStatement& s) { runtimeLoop(s); }
        void handle(const RepeatLoopStatement& s) { runtimeLoop(s); }
        void handle(const ForeverLoopStatement& s) { runtimeLoop(s); }
        void handle(const ForeachLoopStatement& s) { runtimeLoop(s); }
        void handle(const ConditionalExpression& s) { guarded(s); }
        void handle(const BinaryExpression& e) {
            if (e.op == BinaryOperator::LogicalAnd || e.op == BinaryOperator::LogicalOr || e.op == BinaryOperator::LogicalImplication) guarded(e);
            else visitDefault(e);
        }
        int earlyLoops = 0;   // enclosing loops that a break, call or disable may leave early
        int unjudgedLoops = 0;   // enclosing loops (any kind, with or without an exit) slang cannot have unrolled (no constant start, bound and step; while, do, repeat, forever, foreach): its drivers say nothing of an outer exit
        int continueLoops = 0;   // ... of which a `continue` runs in some iteration (or cannot be judged)
        std::vector<const slang::ast::ValueSymbol*> scopeCounters;   // loop counters of the enclosing loops that nothing in their loop writes
        void handle(const ForLoopStatement& f) {
            // Policy: each exit's conditions are evaluated per iteration with the loop variable known and design state unknown. An exit
            // that certainly runs stops the later iterations (their drivers do not exist: Unresolved); one that only may run, depending
            // on design state, leaves every unrolled write in the hardware (the overlap is judged by structure, as for a guarded
            // write); one that cannot be evaluated is Unresolved.
            std::optional<lint::LoopValues> values = lint::constantLoopValues(f, l);
            std::vector<const slang::ast::ValueSymbol*> own(f.loopVars.begin(), f.loopVars.end());
            for (auto* init : f.initializers)
                if (init->kind == ExpressionKind::Assignment && init->as<AssignmentExpression>().left().kind == ExpressionKind::NamedValue)
                    own.push_back(&init->as<AssignmentExpression>().left().as<NamedValueExpression>().symbol);
            std::vector<const slang::ast::ValueSymbol*> vars = scopeCounters;
            for (auto* v : own) if (!values || v != values->var) vars.push_back(v);
            const slang::ast::Statement* scopeBody = nullptr;
            if (auto* pb = container ? container->as_if<ProceduralBlockSymbol>() : nullptr) scopeBody = &pb->getBody();
            else if (auto* sr = container ? container->as_if<SubroutineSymbol>() : nullptr) scopeBody = &sr->getBody();
            const auto exit = lint::earlyExit(l, f.body, values, vars, scopeBody);
            const bool early = exit.leaves && exit.notRuntime;
            earlyLoops += early;
            const bool cont = early && exit.continues;
            continueLoops += cont;
            const bool unjudged = !values;
            unjudgedLoops += unjudged;
            if (early) {
                std::vector<lint::LoopValues> outers;
                for (auto& [var, lv] : loops) outers.push_back(lv);
                profiles.push_back(lint::exitProfile(l, f.body, values, outers, vars, scopeBody));
            }
            struct Restore {
                int& n; bool on; int& k; bool cont; std::vector<const slang::ast::ValueSymbol*>& c; std::vector<const slang::ast::ValueSymbol*> saved;
                std::vector<std::optional<lint::ExitProfile>>& profiles; int& u; bool unjudged;
                ~Restore() { n -= on; k -= cont; u -= unjudged; c = std::move(saved); if (on) profiles.pop_back(); }
            } restore{earlyLoops, early, continueLoops, cont, scopeCounters, scopeCounters, profiles, unjudgedLoops, unjudged};
            scopeCounters = vars;
            if (values) scopeCounters.push_back(values->var);
            std::optional<lint::LoopValues> outer;
            if (values) {
                if (auto it = loops.find(values->var); it != loops.end()) outer = it->second;
                loops[values->var] = *values;
            }
            const bool zeroTrips = !values && lint::neverRuns(f, l);   // its writes are not hardware
            deadDepth += zeroTrips;
            ++guardDepth;
            visitDefault(f);
            --guardDepth;
            deadDepth -= zeroTrips;
            if (values) {
                if (outer) loops[values->var] = *outer;
                else loops.erase(values->var);
            }
        }
        std::set<const Symbol*> withContinue;   // procedures and subroutines that have a `continue`
        struct ContinueFinder : ASTVisitor<ContinueFinder, VisitFlags::AllGood> {
            bool found = false;
            void handle(const ContinueStatement&) { found = true; }
        };
        template<typename T> void within(const T& sym) {
            auto outer = container;
            container = &sym;
            ContinueFinder cf;
            sym.getBody().visit(cf);
            if (cf.found) withContinue.insert(&sym);
            visitDefault(sym);
            container = outer;
        }
        void handle(const ProceduralBlockSymbol& b) { within(b); }
        void handle(const SubroutineSymbol& r) { within(r); }
        void handle(const AssignmentExpression& a) { note(a.left()); visitDefault(a); }
        void handle(const UnaryExpression& u) {
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement ||
                u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Postdecrement) note(u.operand());
            visitDefault(u);
        }
        bool at(const Symbol* writer, const Symbol* var) const { return dynamic.count({writer, var}) > 0; }
    };
    // Instances whose inout port is connected to a net: slang adds no driver to that net for what the child drives through
    // the port, so the child's port net is linked to the connected net here. `segs` say which bit of the port is which bit of
    // the net: port bits [from, to] are net bits starting at `net`. `mapped` is false when the connection is not a net, a
    // constant select of one or a concatenation of those; the child's drivers then reach every bit, possibly releasing.
    struct Seg { uint64_t from, to, net; };
    struct InoutLink { const NetSymbol* child; const Symbol* inst; bool mapped; std::vector<Seg> segs; bool blackBox = false; };
    using InoutLinks = std::map<const NetSymbol*, std::vector<InoutLink>>;
    using BitMap = std::optional<std::vector<Seg>>;   // bits of a net as bits of the net under test; nothing when unmapped
    struct InoutScan : ASTVisitor<InoutScan, VisitFlags::AllGood> {
        InoutLinks links;
        slang::ast::ASTContext ac;
        slang::ast::EvalContext ec;
        explicit InoutScan(Lint& l) : ac(l.compilation().getRoot(), slang::ast::LookupLocation::max), ec(ac) {}
        static bool blackBox(const InstanceSymbol& inst) {
            for (const auto* attribute : inst.getParentScope()->getCompilation().getAttributes(inst.getDefinition())) {
                std::string name(attribute->name);
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto& value = attribute->getValue();
                if ((name == "black_box" || name == "blackbox" || name == "syn_black_box") && !(value.isInteger() && value.integer() == 0)) return true;
            }
            return false;
        }
        // The net a name stands for: a net itself, or the net behind a modport port.
        static const NetSymbol* netOf(const Symbol& sym) {
            const Symbol* s = &sym;
            if (s->kind == SymbolKind::ModportPort) s = s->as<ModportPortSymbol>().internalSymbol;
            return s && s->kind == SymbolKind::Net ? &s->as<NetSymbol>() : nullptr;
        }
        // The nets of a port connection with the port bits each carries (lowest port bit first); false when some part of it is
        // not a net, a constant select of one, or a concatenation of those. A modport port defined by an expression is followed
        // through that expression.
        bool map(const Expression& ex, uint64_t portWidth, std::map<const NetSymbol*, std::vector<Seg>>& out, int depth = 0) {
            if (depth > 8) return false;
            const Expression* u = &ex;
            while (u->kind == ExpressionKind::Conversion || u->kind == ExpressionKind::Assignment)
                u = u->kind == ExpressionKind::Conversion ? &u->as<ConversionExpression>().operand() : &u->as<AssignmentExpression>().left();
            std::vector<const Expression*> ops;
            if (u->kind == ExpressionKind::Concatenation) {
                for (auto* op : u->as<ConcatenationExpression>().operands()) ops.push_back(op);
                std::reverse(ops.begin(), ops.end());   // lowest bits first
            } else ops.push_back(u);
            uint64_t at = 0;
            for (auto* op : ops) {
                const Expression* e = op;
                while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
                slang::ast::ValuePath path(*e, ec);
                if (path.empty() || !path.rootSymbol() || !path.isFullyStatic()) return false;
                const uint64_t width = path.lspBounds.second - path.lspBounds.first + 1;
                const Symbol* rs = path.rootSymbol();
                if (rs->kind == SymbolKind::ModportPort && !rs->as<ModportPortSymbol>().internalSymbol) {
                    const auto* inner = rs->as<ModportPortSymbol>().explicitConnection;
                    std::map<const NetSymbol*, std::vector<Seg>> sub;
                    if (!inner || !map(*inner, rs->as<ValueSymbol>().getType().getSelectableWidth(), sub, depth + 1)) return false;
                    for (auto& [net, segs] : sub)   // the modport port's bits [lowest, highest] are this operand's bits
                        for (auto& sg : segs) {
                            const uint64_t lo = std::max(sg.from, path.lspBounds.first), hi = std::min(sg.to, path.lspBounds.second);
                            if (lo > hi) continue;
                            const uint64_t pos = at + (lo - path.lspBounds.first);
                            if (pos >= portWidth) continue;
                            out[net].push_back({pos, std::min(pos + (hi - lo), portWidth - 1), sg.net + (lo - sg.from)});
                        }
                    at += width;
                    continue;
                }
                const NetSymbol* root = netOf(*rs);
                if (!root) return false;
                if (at < portWidth) {
                    const uint64_t used = std::min(width, portWidth - at);   // a connection wider than the port loses its upper bits
                    out[root].push_back({at, at + used - 1, path.lspBounds.first});
                }
                at += width;
            }
            return true;
        }
        // Every net a connection mentions, through modport ports defined by expressions.
        void netsIn(const Expression& ex, std::map<const NetSymbol*, std::vector<Seg>>& nets, int depth = 0) {
            ex.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                if (sym.kind == SymbolKind::ModportPort && !sym.as<ModportPortSymbol>().internalSymbol) {
                    if (const auto* inner = sym.as<ModportPortSymbol>().explicitConnection; inner && depth < 8) netsIn(*inner, nets, depth + 1);
                } else if (const NetSymbol* net = netOf(sym)) nets[net];
            });
        }
        void handle(const InstanceSymbol& inst) {
            const bool bb = blackBox(inst);
            for (auto* pc : inst.getPortConnections()) {
                if (!pc || pc->port.kind != SymbolKind::Port) continue;
                const auto& port = pc->port.as<PortSymbol>();
                if (port.direction != ArgumentDirection::InOut || !port.internalSymbol || port.internalSymbol->kind != SymbolKind::Net) continue;
                const Expression* ex = pc->getExpression();
                if (!ex) continue;
                // slang analyses only the canonical body of identical instances: the drivers sit on its port net
                const NetSymbol* canon = &port.internalSymbol->as<NetSymbol>();
                const InstanceSymbol& analysed = analysedInstance(inst);
                if (const auto* cb = analysed.getCanonicalBody() ? analysed.getCanonicalBody() : &analysed.body)
                    for (auto& m : cb->members())
                        if (m.kind == SymbolKind::Port && m.name == port.name)
                            if (auto* is = m.as<PortSymbol>().internalSymbol; is && is->kind == SymbolKind::Net) canon = &is->as<NetSymbol>();
                const NetSymbol& child = *canon;
                std::map<const NetSymbol*, std::vector<Seg>> nets;
                const bool mapped = map(*ex, child.getType().getSelectableWidth(), nets);
                if (!mapped) {
                    nets.clear();
                    netsIn(*ex, nets);
                }
                for (auto& [pn, segs] : nets) links[pn].push_back({&child, &inst, mapped, segs, bb});
            }
            visitDefault(inst);
        }
    };
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        const Callers& callers;
        const CounterScan& counters;
        const std::unordered_set<const ValueSymbol*>& temps;   // procedure temporaries (analysis/temps.h)
        const RuntimeIndexScan& runtime;
        const InoutLinks& inoutLinks;
        const PortWrites& portWrites;
        V(Lint& lint, const Callers& c, const CounterScan& f, const std::unordered_set<const ValueSymbol*>& t, const RuntimeIndexScan& r,
          const InoutLinks& io, const PortWrites& pw)
            : l(lint), callers(c), counters(f), temps(t), runtime(r), inoutLinks(io), portWrites(pw) {}
        // `alias` joins nets whose drivers this rule does not merge: a conflict between them is not checked.
        void handle(const NetAliasSymbol& a) {
            if (l.isUser(a.location))
                l.report(a.location, "net alias: drivers of the aliased nets are not combined, so a conflict between them is not checked",
                         {.confidence = Confidence::Unresolved,
                          .nextStep = "Drive the joined nets from one place, or check the drivers of every aliased net by hand."});
            visitDefault(a);
        }
        void handle(const NetSymbol& n) { checkNet(n); visitDefault(n); }
        // slang analyses the body of identical instances once, but it records the drivers a module makes through an interface
        // port on the nets of the interface instance the port is connected to, so every interface instance is its own net.
        // The walk above takes canonical bodies only, which would leave the nets of a second `bus_if` unchecked.
        struct IfaceNets : HardwareVisitor<IfaceNets, VisitFlags::AllGood> {
            V& v;
            explicit IfaceNets(V& vv) : v(vv) {}
            void handle(const NetSymbol& n) { v.checkNet(n); visitDefault(n); }
            void handle(const VariableSymbol& var) { v.checkVar(var); }
        };
        void handle(const InstanceSymbol& inst) {
            if (inst.getDefinition().definitionKind == DefinitionKind::Interface && inst.getCanonicalBody()) {
                IfaceNets nets(*this);   // not visitDefault: that walks the canonical body again, a second report for its nets
                inst.body.visit(nets);
            } else visitDefault(inst);
        }
        void checkNet(const NetSymbol& n) {
            auto* am = l.context().design.analysis.get();
            auto nk = n.netType.netKind;
            if (am && l.isUser(n.location) && isModuleLevel(n) && (nk == NetType::Wire || nk == NetType::Tri || nk == NetType::Tri0 || nk == NetType::Tri1 || nk == NetType::TriReg)) {
                l.count();
                // The continuous drivers: assignments, the net's declaration assignment and gate terminals.
                struct Drv { std::pair<uint64_t, uint64_t> bounds; const Symbol* id; slang::SourceLocation loc; const Expression* assignment; bool releases; bool never; bool port = false; uintptr_t path = 0; const PrimitiveInstanceSymbol* gate = nullptr; bool blackBox = false; bool approx = false; std::pair<int, int> strength{3, 3}; };   // path: the instances a driver is reached through (bodies may be shared)
                std::vector<Drv> drv;
                // `inner`: the net is a child's inout port net reached through a connection; its drivers reach the parent net.
                // slang does not carry them across an inout port (it does across an output port), so they are collected here.
                std::function<void(const NetSymbol&, bool, const BitMap&, uintptr_t, int)> collect = [&](const NetSymbol& net, bool inner, const BitMap& bits, uintptr_t path, int depth) {
                    for (const auto& dr : driversOf(*am, portWrites, net)) {
                        const auto* d = dr.d;
                        if (dr.undecided) {   // attribution through identical instances cannot be confirmed: a possible driver, undecided
                            const uint64_t w = n.getType().getSelectableWidth();
                            drv.push_back({{0, w ? w - 1 : 0}, dr.where, dr.where->location, nullptr, true, false, false, path * 31 + reinterpret_cast<uintptr_t>(dr.where), nullptr, false, true});
                            continue;
                        }
                        if (d->isInputPort()) {
                            // The parent drives this port net, so an assignment inside the child is a second continuous driver.
                            if (!inner)
                                if (const Expression* conn = parentConnection(net))
                                    drv.push_back({d->getBounds(), d->containingSymbol, conn->sourceRange.start(), nullptr, connectionCanRelease(l, *conn), false, true});
                            continue;
                        }
                        // the bits of the net under test this driver reaches; a connection that cannot be mapped reaches every bit, possibly releasing
                        const uint64_t w = n.getType().getSelectableWidth();
                        const bool wide = inner && !bits;
                        std::vector<std::pair<uint64_t, uint64_t>> pieces;
                        if (wide) pieces.push_back({0, w ? w - 1 : 0});
                        else for (auto& sg : *bits) {
                            const auto b = dr.bounds;
                            const uint64_t lo = std::max(b.first, sg.from), hi = std::min(b.second, sg.to);
                            if (lo <= hi) pieces.push_back({sg.net + (lo - sg.from), sg.net + (hi - sg.from)});
                        }
                        for (const auto bnd : pieces) {
                        if (d->flags.has(analysis::DriverFlags::OutputPort)) {   // a nested instance's output: its behaviour is not modelled here
                            if (inner) drv.push_back({bnd, d->containingSymbol, net.location, nullptr, true, false, false, path});
                            continue;
                        }
                        if (d->kind != analysis::DriverKind::Continuous) continue;
                        // A write made through an interface port is the canonical body's, once per instance (PortWriteScan). The only copies
                        // slang makes that are used as they are belong to a `ref` port.
                        std::vector<std::pair<const Symbol*, uintptr_t>> origins{{d->containingSymbol, path}};   // (container, instances reached through)
                        if (dr.site) origins = {{d->containingSymbol, path * 31 + reinterpret_cast<uintptr_t>(dr.site)}};
                        else if (const auto* inst = d->containingSymbol->as_if<InstanceSymbol>(); inst && d->flags.has(analysis::DriverFlags::FromSideEffect)) {
                            origins.clear();
                            const InstanceBodySymbol* body = inst->getCanonicalBody();
                            if (!body) body = &inst->body;
                            if (auto state = am->getInstanceDriverState(*body))
                                for (auto& hp : state->hierPortDrivers) {
                                    const auto* o = hp.driver.get();
                                    if (!hp.ref && sameWrite(*o, *d, net)) origins.push_back({o->containingSymbol, path * 31 + reinterpret_cast<uintptr_t>(inst)});
                                }
                            if (origins.empty()) drv.push_back({bnd, inst, inst->location, nullptr, true, false, false, path, nullptr, false, true});   // not traceable: may release, undecided
                        }
                        for (auto [container, dpath] : origins) {
                            if (auto* ca = container->as_if<ContinuousAssignSymbol>()) {
                                const auto& asg = ca->getAssignment();
                                const Expression& rhs = asg.kind == ExpressionKind::Assignment ? asg.as<AssignmentExpression>().right() : asg;
                                const auto rank = driveRank(ca->getDriveStrength());   // a highz0 / highz1 driver releases the net on that value
                                drv.push_back({bnd, ca, ca->location, &asg, ZSearchFn(l, rhs) || wide || rank.first == 0 || rank.second == 0, constantZ(l, asg), false, dpath, nullptr, false, false, rank});
                            } else if (auto* gate = container->as_if<PrimitiveInstanceSymbol>()) {
                                const std::string_view kind = gate->primitiveType.name;
                                if (kind == "pullup" || kind == "pulldown") continue;   // weak: resolved by strength, never a conflict
                                // a switch or tri-state gate can release the net; a UDP or logic gate always drives
                                const auto rank = driveRank(gate->getDriveStrength());
                                drv.push_back({bnd, gate, gate->location, nullptr, gateReleases(kind) || wide || rank.first == 0 || rank.second == 0, false, false, dpath, gate, false, false, rank});
                            } else if (d->flags.has(analysis::DriverFlags::Initializer) && net.getInitializer()) {   // `wire w = a;`
                                const Expression& init = *net.getInitializer();
                                bool never = false;
                                if (auto cv = l.evalConst(init); cv.isInteger() && cv.integer().getBitWidth() > 0 && allZ(cv.integer())) never = true;
                                const auto rank = driveRank(net.getDriveStrength());
                                drv.push_back({bnd, &net, net.location, nullptr, ZSearchFn(l, init) || wide || rank.first == 0 || rank.second == 0, never, false, dpath, nullptr, false, false, rank});
                            }
                        }
                        }
                    }
                    if (depth > 8) return;
                    if (auto it = inoutLinks.find(&net); it != inoutLinks.end())
                        for (auto& link : it->second) {
                            // the port bits of the child as bits of the net under test
                            BitMap mine;
                            if (bits && link.mapped) {
                                mine.emplace();
                                for (auto& sg : link.segs)
                                    for (auto& o : *bits) {
                                        const uint64_t lo = std::max(sg.net, o.from), hi = std::min(sg.net + (sg.to - sg.from), o.to);
                                        if (lo <= hi) mine->push_back({sg.from + (lo - sg.net), sg.from + (hi - sg.net), o.net + (lo - o.from)});
                                    }
                            }
                            const uintptr_t through = path * 31 + reinterpret_cast<uintptr_t>(link.inst);
                            if (link.blackBox) {   // what a black box drives is not modelled: it may drive any bit it is connected to
                                if (mine) for (auto& sg : *mine) drv.push_back({{sg.net, sg.net + (sg.to - sg.from)}, link.inst, link.inst->location, nullptr, true, false, false, through, nullptr, true});
                                else drv.push_back({{0, n.getType().getSelectableWidth() ? n.getType().getSelectableWidth() - 1 : 0}, link.inst, link.inst->location, nullptr, true, false, false, through, nullptr, true});
                            } else collect(*link.child, true, mine, through, depth + 1);
                        }
                };
                BitMap identity;
                identity.emplace();
                identity->push_back({0, n.getType().getSelectableWidth() ? n.getType().getSelectableWidth() - 1 : 0, 0});
                collect(n, false, identity, 0, 0);
                auto reportStrengths = [&](const Drv& a, const Drv& b) {
                    l.report(a.loc < b.loc ? b.loc : a.loc, "net '" + std::string(n.name) + "' has several overlapping drivers with drive strengths: drive strengths resolve this net in simulation; synthesis may ignore strengths",
                             {.confidence = Confidence::Unresolved,
                              .nextStep = "Drive the net from one source, or make the drivers tri-state with exclusive enables; do not rely on drive strengths.",
                              .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                };
                for (size_t i = 0; i < drv.size(); i++)
                    for (size_t j = i + 1; j < drv.size(); j++) {
                        const Drv &a = drv[i], &b = drv[j];
                        if ((a.id == b.id && a.path == b.path) || a.bounds.second < b.bounds.first || b.bounds.second < a.bounds.first) continue;
                        if (a.never || b.never) continue;   // a driver that never drives
                        if (a.approx || b.approx) {   // a driver whose instance or net cannot be confirmed counts, but never as an ERROR
                            l.report(a.loc < b.loc ? b.loc : a.loc, "net '" + std::string(n.name) + "' may have several overlapping drivers: driver attribution through identical instances is approximate, so a conflict cannot be excluded",
                                     {.confidence = Confidence::Unresolved,
                                      .nextStep = "Check the writes through the interface ports of the identical instances by hand, or drive the net from one place.",
                                      .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                            return;
                        }
                        // A driver weaker for both values loses to the other in simulation (a keeper), but synthesis may ignore
                        // strengths and build two drivers: never silent (pullup / pulldown primitives are skipped above).
                        if ((a.strength.first > b.strength.first && a.strength.second > b.strength.second) ||
                            (b.strength.first > a.strength.first && b.strength.second > a.strength.second)) { reportStrengths(a, b); return; }
                        // Equal strengths (weak against weak, supply against supply) are unordered, as strong against strong is.
                        const bool strengths = a.strength != b.strength;
                        if (a.releases || b.releases) {   // a possible z drive: a tri-state bus; the drivers must never be enabled together
                            if (a.assignment && b.assignment && provablyExclusiveEnables(l, *a.assignment, *b.assignment)) continue;
                            if (a.gate && b.gate && gatesExclusive(*a.gate, *b.gate)) continue;
                            if (strengths) { reportStrengths(a, b); return; }
                            const bool bb = a.blackBox || b.blackBox;
                            l.report(a.loc < b.loc ? b.loc : a.loc, "net '" + std::string(n.name) + "' has several overlapping tri-state drivers and their enables are not provably exclusive: a driver conflict cannot be excluded" +
                                     (bb ? "; a black box may drive this net and what it drives is not modelled" : ""),
                                     {.confidence = Confidence::Unresolved,
                                      .nextStep = "Make the enables mutually exclusive by construction (one select, decoded), or prove it with an assertion.",
                                      .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                            return;
                        }
                        if (strengths) { reportStrengths(a, b); return; }   // a pair the strengths do not order for both values
                        l.report(a.loc < b.loc ? b.loc : a.loc, "net '" + std::string(n.name) + "' has several overlapping continuous drivers and none of them can release it (no z): a driver conflict",
                                 {.severity = Severity::Error, .nextStep = "Drive the net from one source, or make the drivers tri-state.", .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                        return;
                    }
            }
        }
        // (d) A variable written to overlapping bits by two plain `always` procedures: two drivers in hardware, and a race
        // in simulation. slang reports the always_ff / always_comb / always_latch forms; an `initial` is an initial value.
        void handle(const VariableSymbol& v) { checkVar(v); }
        void checkVar(const VariableSymbol& v) {
            auto* am = l.context().design.analysis.get();
            // A subroutine's or block's own variable is not state shared between procedures (synthesis inlines calls).
            if (am && l.isUser(v.location) && isModuleLevel(v)) {
                struct Writer { std::pair<uint64_t, uint64_t> bounds; const Symbol* id; bool fixed; const Symbol* via; bool untraced = false; bool floor = false; bool wide = false; };   // id: the procedure, or the instance it is reached through; via: where the write is made
                std::vector<Writer> procs;
                const auto drivers = driversOf(*am, portWrites, v);
                // a procedure temporary: every procedure writes it before it reads it and nothing else reads it (analysis/temps.h), so it
                // carries no value between processes; drivers elsewhere (a port, a continuous assignment) would be writes the scan did not see
                if (!drivers.empty() && (temps.count(&v) || counters.isCounter(&v)) &&
                    std::all_of(drivers.begin(), drivers.end(), [](const DriverRef& dr) {
                        return !dr.undecided && !dr.site && (dr.d->containingSymbol->template as_if<ProceduralBlockSymbol>() || dr.d->containingSymbol->template as_if<SubroutineSymbol>()); })) {
                    visitDefault(v);
                    return;
                }
                // A procedure or subroutine that writes the variable through a loop counter: slang's driver for it is the whole
                // variable, so its writes are taken from the scan, with the bits the loop reaches.
                auto byScan = [&](const Symbol* c) {
                    auto it = runtime.writes.find({c, &v});
                    return it != runtime.writes.end() && it->second.counter;
                };
                std::set<std::pair<const Symbol*, const Symbol*>> scanned;   // (container, instance or null)
                // A write under a loop whose `continue` runs in some iteration: slang's driver stops at that iteration, so the bits
                // the loop may reach come from the scan, as an undecided writer (never an ERROR, never silence).
                std::set<std::pair<const Symbol*, const Symbol*>> widened;
                std::map<const Symbol*, std::vector<std::pair<uint64_t, uint64_t>>> slangBounds;   // slang's drivers of each always_comb / _ff / _latch writer
                auto widen = [&](const Symbol* container, const Symbol* id) {
                    auto it = runtime.writes.find({container, &v});
                    if (it == runtime.writes.end() || it->second.wide.empty() || !widened.insert({container, id}).second) return;
                    for (auto& iv : it->second.wide) procs.push_back({iv, id, false, container, false, false, true});
                };
                // `container`: the procedure or subroutine that makes the write; `instance`: the instance the write is reached
                // through when the variable is an interface or hierarchical target (the same body in two instances is two writers).
                std::map<const Symbol*, std::vector<std::pair<uint64_t, uint64_t>>> slangOf;   // the bits slang lists a driver for, per procedure or subroutine
                auto addWriter = [&](const Symbol* container, const Symbol* instance, std::pair<uint64_t, uint64_t> bounds) {
                    slangOf[container].push_back(bounds);
                    if (auto* blk = container->as_if<ProceduralBlockSymbol>(); blk && blk->procedureKind != ProceduralBlockKind::Always) {
                        // `initial` gives an initial value; always_comb / always_ff / always_latch are slang's diagnostics, but built on drivers
                        // that stop at a decided `continue`: those writes join as undecided writers, and a pair slang's own drivers overlap
                        // is slang's to report
                        using K = ProceduralBlockKind;
                        if (blk->procedureKind == K::AlwaysComb || blk->procedureKind == K::AlwaysFF || blk->procedureKind == K::AlwaysLatch) {
                            slangBounds[instance ? instance : container].push_back(bounds);
                            widen(container, instance ? instance : container);
                        }
                        return;
                    }
                    if (byScan(container)) { scanned.insert({container, instance}); return; }
                    if (auto* block = container->as_if<ProceduralBlockSymbol>()) {
                        if (block->procedureKind == ProceduralBlockKind::Always) {
                            procs.push_back({bounds, instance ? instance : block, !runtime.at(block, &v), block});
                            widen(block, instance ? instance : block);
                            // A write no condition or loop guards runs whatever the procedure's other writes do: it decides the overlap itself.
                            if (runtime.at(block, &v) && !lint::leavesEarly(block->getBody(), true))
                                if (auto it = runtime.writes.find({block, &v}); it != runtime.writes.end())
                                    for (auto& iv : it->second.certain) procs.push_back({iv, instance ? instance : block, true, block});
                        }
                    // a subroutine's own variables are per call; only state outside it is shared between its callers
                    } else if (auto* routine = container->as_if<SubroutineSymbol>(); routine && isModuleLevel(v)) {
                        if (auto it = callers.find(routine); it != callers.end())
                            for (auto* caller : it->second) {
                                procs.push_back({bounds, instance ? instance : caller, !runtime.at(routine, &v), routine});
                                widen(routine, instance ? instance : caller);
                            }
                    }
                };
                for (const auto& dr : drivers) {
                    if (dr.undecided) {   // attribution through identical instances cannot be confirmed: a possible writer, undecided
                        procs.push_back({dr.bounds, dr.where, false, dr.where, true});
                        continue;
                    }
                    const auto* d = dr.d;
                    // A write made through an interface port is the canonical body's, once per instance (PortWriteScan).
                    if (dr.site) {
                        if (d->kind == analysis::DriverKind::Procedural) addWriter(d->containingSymbol, dr.site, dr.bounds);
                        continue;
                    }
                    // The only copies slang makes that are used as they are belong to a `ref` port.
                    const auto* inst = d->containingSymbol->as_if<InstanceSymbol>();
                    if (inst && d->flags.has(analysis::DriverFlags::FromSideEffect) && d->kind == analysis::DriverKind::Procedural) {
                        bool found = false;
                        const InstanceBodySymbol* body = inst->getCanonicalBody();
                        if (!body) body = &inst->body;
                        if (auto state = am->getInstanceDriverState(*body))
                            for (auto& hp : state->hierPortDrivers) {
                                const auto* o = hp.driver.get();
                                if (hp.ref || !sameWrite(*o, *d, v)) continue;
                                addWriter(o->containingSymbol, inst, d->getBounds());
                                found = true;
                            }
                        if (!found) procs.push_back({d->getBounds(), inst, false, inst, true});   // not traceable to its procedure: undecided
                        continue;
                    }
                    addWriter(d->containingSymbol, nullptr, d->getBounds());
                }
                for (auto& [c, instance] : scanned) {
                    const auto& w = runtime.writes.at({c, &v});
                    std::vector<const Symbol*> ids;
                    if (instance) ids.push_back(instance);
                    else if (c->kind == SymbolKind::ProceduralBlock) ids.push_back(c);
                    else if (auto it = callers.find(&c->as<SubroutineSymbol>()); it != callers.end()) ids.assign(it->second.begin(), it->second.end());
                    for (auto* id : ids) {
                        for (auto& iv : w.fixed) procs.push_back({iv, id, true, c});
                        for (auto& iv : w.dyn) procs.push_back({iv, id, false, c});
                    }
                }
                // A loop left early by exits that can all be judged: slang's drivers are exact for the iterations that run. When every
                // driver of the procedure lies within the bits the prediction (or a write outside such loops) reaches, slang followed the
                // exits as the prediction does, and its bounds are writes in the hardware: the overlap is judged by structure.
                for (auto& w : procs) {
                    if (w.fixed || w.floor || w.wide || w.untraced || !w.via || !runtime.dynamic.count({w.via, &v}) || runtime.indexDynamic.count({w.via, &v})) continue;
                    auto lit = runtime.live.find({w.via, &v});
                    if (lit == runtime.live.end() || lit->second.opaque) continue;
                    std::vector<std::pair<uint64_t, uint64_t>> allowed = lit->second.bits;
                    if (auto wit = runtime.writes.find({w.via, &v}); wit != runtime.writes.end())
                        allowed.insert(allowed.end(), wit->second.fixed.begin(), wit->second.fixed.end());
                    std::sort(allowed.begin(), allowed.end());
                    auto within = [&](std::pair<uint64_t, uint64_t> b) {
                        uint64_t at = b.first;
                        for (auto& iv : allowed) {
                            if (iv.second < at) continue;
                            if (iv.first > at) return false;
                            if (iv.second >= b.second) return true;
                            at = iv.second + 1;
                        }
                        return false;
                    };
                    bool exact = true;
                    for (auto& b : slangOf[w.via]) exact = exact && within(b);
                    if (exact) w.fixed = true;
                }
                // The syntactic floor: a write in the code that slang lists no driver for, or a narrower one than the select names. It joins
                // as a writer, certain only where no condition, loop or exit guards it (a write after a loop that has a `continue`), else
                // undecided. Not followed: a write made through an interface or hierarchical reference (PortWriteScan), and the code of
                // a branch a constant condition excludes.
                if (auto fit = runtime.floorContainers.find(&v); fit != runtime.floorContainers.end()) {
                    for (const Symbol* c : fit->second) {
                        if (byScan(c) || enclosingBody(*c) != enclosingBody(v)) continue;
                        const auto* block = c->as_if<ProceduralBlockSymbol>();
                        const auto* routine = c->as_if<SubroutineSymbol>();
                        if (block ? block->procedureKind == ProceduralBlockKind::Initial || block->procedureKind == ProceduralBlockKind::Final
                                  : !routine)
                            continue;
                        std::vector<std::pair<uint64_t, uint64_t>> listed;
                        if (auto it = slangOf.find(c); it != slangOf.end()) listed = it->second;
                        std::vector<const Symbol*> ids;
                        if (block) ids.push_back(block);
                        else if (auto it = callers.find(routine); it != callers.end()) {
                            ids.assign(it->second.begin(), it->second.end());
                            for (auto* caller : ids)   // slang may list a subroutine's write at the call site
                                if (auto cit = slangOf.find(caller); cit != slangOf.end()) listed.insert(listed.end(), cit->second.begin(), cit->second.end());
                        }
                        std::sort(listed.begin(), listed.end());
                        auto covered = [&](std::pair<uint64_t, uint64_t> b) {
                            uint64_t at = b.first;
                            for (auto& iv : listed) {
                                if (iv.second < at) continue;
                                if (iv.first > at) return false;
                                if (iv.second >= b.second) return true;
                                at = iv.second + 1;
                            }
                            return false;
                        };
                        const bool whole = block && !lint::leavesEarly(block->getBody(), true);
                        auto overlapsListed = [&](std::pair<uint64_t, uint64_t> b) {
                            for (auto& iv : listed) if (iv.first <= b.second && b.first <= iv.second) return true;
                            return false;
                        };
                        for (const auto& fw : runtime.floor.at({c, &v})) {
                            if (fw.exitOnly || covered(fw.bounds)) continue;
                            // slang unrolls a loop with an index exactly, so a narrower driver than the select names is its own count of
                            // the iterations, except where a `continue` makes it stop short
                            if (!fw.isStatic && !runtime.withContinue.count(c) && overlapsListed(fw.bounds)) continue;
                            for (auto* id : ids) procs.push_back({fw.bounds, id, whole && fw.isStatic && fw.certain, c, false, true});
                        }
                    }
                }
                // Overlap decided by constants is a proven conflict. A write whose element or bit is chosen at run time
                // (a memory address, `y[i]`) overlaps another writer's bits only if the indexes collide, except where the
                // other writer provably writes them whatever the index is:
                //  - it writes every bit or word of the variable (a whole write, or writes that together cover it);
                //  - both write a packed vector and the bits the two selects can reach (the selector is a*x+b of one
                //    narrow variable, every value tried) share a bit.
                // Packed bits are flip-flops in both processes, so a shared reachable bit is two drivers. Unpacked memories
                // stay undecided unless one side covers all words: two ports writing a memory can be a true dual-port RAM.
                using Iv = RuntimeIndexScan::Iv;
                const uint64_t total = v.getType().getSelectableWidth();
                auto normalize = [](std::vector<Iv> iv) {
                    std::sort(iv.begin(), iv.end());
                    std::vector<Iv> out;
                    for (auto& x : iv) {
                        if (!out.empty() && x.first <= out.back().second + 1) out.back().second = std::max(out.back().second, x.second);
                        else out.push_back(x);
                    }
                    return out;
                };
                auto coversAll = [&](const Symbol* blk) {
                    std::vector<Iv> iv;
                    for (auto& w : procs) if (w.id == blk && w.fixed) iv.push_back(w.bounds);
                    iv = normalize(iv);
                    return total > 0 && iv.size() == 1 && iv[0].first == 0 && iv[0].second + 1 >= total;
                };
                auto intersects = [](const std::vector<Iv>& a, const std::vector<Iv>& b) {
                    for (auto& x : a) for (auto& y : b) if (x.first <= y.second && y.first <= x.second) return true;
                    return false;
                };
                const bool packed = !v.getType().isUnpackedArray();
                // the reachable bits of a writer entry, or nothing when they are not known
                auto reachOf = [&](const Writer& w) -> std::optional<std::vector<Iv>> {
                    if (w.fixed) return std::vector<Iv>{w.bounds};
                    if (w.floor) return std::nullopt;   // its bits are a superset of the select, not a reach
                    auto it = runtime.reach.find({w.via, &v});
                    if (!packed || it == runtime.reach.end() || it->second.opaque || it->second.iv.empty()) return std::nullopt;
                    return normalize(it->second.iv);
                };
                const Symbol* undecided = nullptr;
                bool unlisted = false;   // ... because a write slang's drivers do not list may meet the other
                bool byExit = false;   // the undecided pair is undecided because a loop may be left early, not because of a run-time index
                bool untraced = false;   // the undecided pair involves a write made through a port whose procedure was not found
                for (size_t i = 0; i < procs.size(); i++)
                    for (size_t j = i + 1; j < procs.size(); j++) {
                        auto [a, b] = std::pair{procs[i], procs[j]};
                        if (a.id == b.id || a.bounds.second < b.bounds.first || b.bounds.second < a.bounds.first) continue;
                        auto slangSees = [&](const Writer& x, const Writer& y) {
                            auto it = slangBounds.find(x.id);
                            if (it == slangBounds.end()) return false;
                            for (auto& iv : it->second) if (iv.first <= y.bounds.second && y.bounds.first <= iv.second) return true;
                            return false;
                        };
                        if (slangSees(a, b) || slangSees(b, a)) continue;
                        const auto& later = a.id->location < b.id->location ? *b.id : *a.id;
                        if (!a.fixed || !b.fixed) {
                            bool proven = (a.fixed && coversAll(a.id)) || (b.fixed && coversAll(b.id));
                            if (!proven) {
                                auto ra = reachOf(a), rb = reachOf(b);
                                proven = ra && rb && intersects(*ra, *rb);
                                if (ra && rb && !proven) continue;   // the bits both can reach are disjoint
                            }
                            if (!proven) {
                                if (!undecided || later.location < undecided->location) {
                                    undecided = &later; untraced = a.untraced || b.untraced;
                                    auto exitOnly = [&](const Writer& x) { return !x.fixed && runtime.dynamic.count({x.via, &v}) && !runtime.indexDynamic.count({x.via, &v}); };
                                    byExit = (a.fixed || exitOnly(a)) && (b.fixed || exitOnly(b));
                                    unlisted = (a.floor && !a.fixed) || (b.floor && !b.fixed);
                                }
                                continue;
                            }
                        }
                        l.report(later.location, "variable '" + std::string(v.name) + "' is written by more than one always procedure on overlapping bits: "
                                 "multiple drivers in hardware and a race in simulation",
                                 {.severity = Severity::Error, .nextStep = "Write the variable from one procedure; merge the conditions that set and clear it.",
                                  .hierarchy = v.getHierarchicalPath(), .signals = {std::string(v.name)}});
                        visitDefault(v);
                        return;
                    }
                if (undecided) {
                    const bool memory = v.getType().isUnpackedArray();
                    l.report(undecided->location,
                             memory ? "memory '" + std::string(v.name) + "' is written by more than one always procedure at addresses chosen at run time: driver overlap is not determinable. "
                                      "Two procedures writing one memory is a true dual-port RAM, which the target must provide; otherwise the writes are multiple drivers"
                                    : untraced ? "variable '" + std::string(v.name) + "' is written through an interface or hierarchical reference by a procedure that could not be traced: driver overlap is not determinable"
                                    : unlisted ? "variable '" + std::string(v.name) + "' is written by more than one always procedure, one of them in code slang's driver analysis drops (after a `continue` that runs, or in a loop it takes for never executing): which bits that write reaches, and so whether the writes overlap, is not determinable"
                                    : byExit ? "variable '" + std::string(v.name) + "' is written by more than one always procedure, one of them in a loop that may be left early: which iterations write, and so whether the writes overlap, is not determinable"
                                    : "variable '" + std::string(v.name) + "' is written by more than one always procedure through an index chosen at run time: driver overlap is not determinable",
                             {.confidence = Confidence::Unresolved,
                              .nextStep = memory ? "Check that the target has a true dual-port RAM (or map the memory to one), and that the two ports never write one address in the same cycle."
                                                 : "Write the variable from one procedure, or make the index ranges disjoint by construction.",
                              .hierarchy = v.getHierarchicalPath(), .signals = {std::string(v.name)}});
                }
            }
            visitDefault(v);
        }
    };
    void check(Lint& l) override {
        const Callers callers = callersOf(l);
        CounterScan counters;
        l.visitHardware(counters);
        RuntimeIndexScan runtime(l, counters);
        l.visitHardware(runtime);
        InoutScan inouts(l);
        l.visitHardware(inouts);
        PortWriteScan portWrites(l, *l.context().design.analysis);
        l.visitHardware(portWrites);
        visitDesign<V>(l, callers, counters, model::getModel(l.context().design).procedureTemps, runtime, inouts.links, portWrites.writes);
        // writes that cannot be attributed to any net or variable are not silent
        const char* next = "Write through the interface ports of identical instances from one place, or check the nets they reach by hand.";
        std::set<std::pair<const void*, const void*>> said;
        for (auto& [origin, site] : portWrites.writes.lost)
            if (said.insert({origin, site}).second && l.isUser(origin->containingSymbol->location))
                l.report(origin->containingSymbol->location, "a write through an interface port cannot be followed to the net or variable it drives in '" + std::string(site->name) +
                         "': driver attribution through identical instances is approximate, so a conflict cannot be excluded",
                         {.confidence = Confidence::Unresolved, .nextStep = next, .hierarchy = site->getHierarchicalPath()});
        for (const auto* site : portWrites.writes.unmapped)
            if (l.isUser(site->location))
                l.report(site->location, "instance '" + std::string(site->name) + "' repeats a body slang analysed once, and its place in that body was not found: "
                         "driver attribution through identical instances is approximate, so a conflict through its interface ports cannot be excluded",
                         {.confidence = Confidence::Unresolved, .nextStep = next, .hierarchy = site->getHierarchicalPath()});
        reportOutputPortOverlaps(l);
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintModelRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<UnconnectedRule>());
    v.push_back(std::make_unique<UnusedRule>());
    v.push_back(std::make_unique<NetConflictRule>());
    v.push_back(std::make_unique<CouplingRule>(CouplingRule::ClockAsData));
    v.push_back(std::make_unique<CouplingRule>(CouplingRule::ResetSteersClock));
    v.push_back(std::make_unique<FsmRule>(FsmRule::Structural));
    v.push_back(std::make_unique<FsmRule>(FsmRule::Obligation));
    v.push_back(std::make_unique<InitRule>());
    v.push_back(std::make_unique<MemoryRule>(true));
    v.push_back(std::make_unique<MemorySemanticsRule>());
    v.push_back(std::make_unique<XControlRule>());
    v.push_back(std::make_unique<RaceRule>());
    return v;
}

} // namespace sverity
