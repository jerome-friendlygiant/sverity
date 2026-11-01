// Native lint on the shared model and the AST: clock/reset coupling (LNT-027), FSM reachability obligations (LNT-029),
// initialisation assumptions (LNT-048), memories (LNT-049), X-sensitive control (LNT-054)
// and sequential races (LNT-060).
#include <map>
#include <set>

#include "analysis/guards.h"
#include "domains/model.h"
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
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/AllSyntax.h"

namespace sverity {
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
        auto reachesClock = [&](model::NodeId from, bool& dataSink) {
            std::vector<model::NodeId> st{from};
            std::set<model::NodeId> seen{from};
            bool clockReached = false;
            while (!st.empty()) {
                auto n = st.back(); st.pop_back();
                for (auto ei : m.out[n]) {
                    const auto& e = m.edges[ei];
                    if (e.type == model::EdgeType::AsyncCtl) continue;
                    if (e.type == model::EdgeType::Seq) { dataSink = true; continue; }
                    if (clocks.count(e.to) && e.to != from) { clockReached = true; continue; }
                    if (seen.insert(e.to).second) st.push_back(e.to);
                }
                if (n < m.topOutput.size() && m.topOutput[n] && n != from) dataSink = true;
            }
            return clockReached;
        };
        for (auto c : clocks) {
            if (kind_ != ClockAsData) break;
            l.count();
            bool data = false;
            reachesClock(c, data);
            if (!data) continue;
            const model::ProcInfo* where = nullptr;
            for (auto ei : m.out[c]) { auto& e = m.edges[ei]; if (e.type != model::EdgeType::AsyncCtl) { where = &m.procs[e.proc]; break; } }
            l.reportAt(where ? where->loc : SourceLocation{}, "clock " + m.nameOf(c) + " is used as data (it reaches the data input of a register or an output)",
                       {.nextStep = "Clocks should only drive clock pins and clock-gating logic; sample clock-derived information with a synchronised register.",
                        .hierarchy = where ? where->instancePath : std::string{}, .signals = {m.nameOf(c)}});
        }
        for (auto r : resets) {
            if (kind_ != ResetSteersClock) break;
            bool data = false;
            if (!reachesClock(r, data)) continue;
            l.count();
            l.reportAt({}, "reset " + m.nameOf(r) + " feeds the logic that generates a clock: a reset must not gate a clock",
                       {.nextStep = "Keep clock generation independent of reset logic.", .signals = {m.nameOf(r)}});
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-029: FSM state encodings (enum types assigned in clocked code and used as case expressions): dead states are
// reported (LNT-029); reachability and liveness are always emitted as proof obligations (LNT-071, never proven here).
class FsmRule final : public LintRule {
public:
    enum Kind { Structural, Obligation };
    explicit FsmRule(Kind k) : LintRule(k == Structural ? "LNT-029" : "LNT-071"), kind_(k) {}
    Kind kind_;
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Type*, std::set<uint64_t>> assigned;      // constants ever assigned to a variable of the enum type
        std::map<const Type*, bool> nonConstAssign, clocked;
        std::map<const Type*, const Symbol*> example;
        std::vector<const CaseStatement*> cases;
        std::set<const CaseStatement*> defaultPrecedes;   // an assignment to a variable of the case's enum type comes before the case in its procedure
        const ProceduralBlockSymbol* curProc = nullptr;
        explicit V(Lint& lint) : l(lint) {}
        static const Type* enumTypeOf(const Expression& e) {
            const Type& t = e.type->getCanonicalType();
            return t.isEnum() ? &t : nullptr;
        }
        void noteAssign(const Expression& left, const Expression& right, bool seq) {
            const Type* t = enumTypeOf(left);
            if (!t) return;
            if (auto* s = left.getSymbolReference()) example.emplace(t, s);
            if (seq) clocked[t] = true;
            if (!collect(right, t)) nonConstAssign[t] = true;
        }
        // collects the enumerator values an expression can produce; false when it can produce anything else (unknown source)
        bool collect(const Expression& e, const Type* t) {
            const Expression* x = &e;
            while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
            if (x->kind == ExpressionKind::ConditionalOp) {
                auto& c = x->as<ConditionalExpression>();
                bool a = collect(c.left(), t), b = collect(c.right(), t);
                return a && b;
            }
            auto cv = l.evalConst(*x);
            if (cv.isInteger() && !cv.integer().hasUnknown()) {
                if (auto v = cv.integer().as<uint64_t>()) assigned[t].insert(*v);
                return true;
            }
            return enumTypeOf(*x) == t && x->kind == ExpressionKind::NamedValue;   // another state variable of the same type
        }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                Assigns a;
                p.getBody().visit(a);
                bool seq = isClocked(p);
                for (auto* x : a.list) noteAssign(x->left(), x->right(), seq);
            }
            auto* saved = curProc;
            curProc = &p;
            visitDefault(p);
            curProc = saved;
        }
        void handle(const VariableSymbol& v) {
            if (l.isUser(v.location) && v.getInitializer() && isModuleLevel(v)) noteAssign(*v.getInitializer(), *v.getInitializer(), false);   // initialiser value
            visitDefault(v);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                cases.push_back(&c);
                if (curProc)
                    if (auto* t = enumTypeOf(c.expr)) {
                        Assigns a;
                        curProc->getBody().visit(a);
                        for (auto* x : a.list)
                            if (enumTypeOf(x->left()) == t && x->sourceRange.start().buffer() == c.sourceRange.start().buffer() &&
                                x->sourceRange.start().offset() < c.sourceRange.start().offset())
                                defaultPrecedes.insert(&c);
                    }
            }
            visitDefault(c);
        }
    };
    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
        std::vector<std::pair<const Type*, const CaseStatement*>> fsmCase;
        for (auto* c : v.cases) if (auto* t = V::enumTypeOf(c->expr)) fsmCase.push_back({t, c});
        for (auto& [t, c] : fsmCase) {
            if (!v.clocked[t]) continue;   // not a registered state: a decode, not an FSM
            l.count();
            std::string dead, all;
            for (auto& ev : t->as<EnumType>().values()) {
                auto cv = ev.getValue();
                auto val = cv.isInteger() ? cv.integer().as<uint64_t>() : std::nullopt;
                all += (all.empty() ? "" : ", ") + std::string(ev.name);
                if (val && !v.assigned[t].count(*val) && !v.nonConstAssign[t])
                    dead += (dead.empty() ? "" : ", ") + std::string(ev.name);
            }
            if (!dead.empty())
                if (kind_ == Structural) l.report(c->sourceRange.start(), "FSM state(s) [" + dead + "] are never assigned: unreachable unless they are the power-up value",
                         {.confidence = Confidence::Heuristic, .nextStep = "Remove the state or add the missing transition.", .signals = {dead}});
            // stuck-state candidates: a case arm that selects one state and contains no assignment to a state variable at all
            for (auto& g : c->items) {
                Assigns a;
                g.stmt->visit(a);
                bool any = false;
                for (auto* x : a.list) any = any || V::enumTypeOf(x->left()) == t;
                if (!any && !v.defaultPrecedes.count(c) && g.expressions.size() == 1 && g.stmt->kind != StatementKind::Empty)
                    if (kind_ == Structural) l.report(g.expressions[0]->sourceRange.start(), "FSM state arm has no transition: the state can never be left unless an assignment to the state precedes the case",
                             {.confidence = Confidence::Heuristic, .nextStep = "Add the exit transition, or make the hold explicit.", .signals = {}});
            }
            if (kind_ == Obligation) l.report(c->sourceRange.start(), "FSM over states [" + all + "]: reachability of every state and absence of deadlock are not proven",
                     {.severity = Severity::Info, .confidence = Confidence::Heuristic, .proof = ProofState::NotAttempted,
                      .nextStep = "Check with formal reachability or simulation coverage."});
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-048: registers that rely on an initial value (declaration initialiser or initial block)
class InitRule final : public LintRule {
public:
    InitRule() : LintRule("LNT-048") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
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
                     {.severity = sev, .confidence = conf, .nextStep = "Add a reset, or select --profile fpga if the init value is intended.", .signals = {name}});
        }
        void handle(const VariableSymbol& v) {
            if (isModuleLevel(v) && v.getInitializer() && v.kind == SymbolKind::Variable && !v.getType().isUnpackedArray()) {
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
                    if (auto* s = x->left().getSymbolReference(); s && isModuleLevel(*s)) note(x->sourceRange.start(), std::string(s->name), "an initial block");
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override {
        V v(l, l.context().config.profile);
        l.compilation().getRoot().visit(v);
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-049: memories (unpacked arrays) with several write processes or combinational writes
class MemoryRule final : public LintRule {
public:
    MemoryRule() : LintRule("LNT-049") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        static bool isMem(const Symbol* s) { auto* v = s ? s->as_if<VariableSymbol>() : nullptr; return v && isModuleLevel(*v) && v->getType().isUnpackedArray(); }
        template <typename N> void asyncRead(const N& e) {
            Reads r;
            e.visit(r);
            for (auto* ref : r.refs)
                if (auto* sym = ref->getSymbolReference(); isMem(sym) && l.isUser(ref->sourceRange.start())) {
                    l.count();
                    l.report(ref->sourceRange.start(), "memory '" + std::string(sym->name) + "' is read asynchronously (combinational read): it maps to distributed/LUT RAM, not a synchronous block RAM",
                             {.severity = Severity::Info, .confidence = Confidence::Informational, .nextStep = "Register the read data if a block RAM is intended.", .signals = {std::string(sym->name)}});
                }
        }
        void handle(const ContinuousAssignSymbol& ca) { asyncRead(ca.getAssignment()); visitDefault(ca); }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                if (!isClocked(p) && p.procedureKind != ProceduralBlockKind::Initial && p.procedureKind != ProceduralBlockKind::Final) asyncRead(p.getBody().kind == StatementKind::Timed ? p.getBody().as<TimedStatement>().stmt : p.getBody());
                else if (isClocked(p)) {
                    Assigns a;
                    p.getBody().visit(a);
                    Reads r;
                    p.getBody().visit(r);
                    std::set<const Symbol*> written;
                    for (auto* x : a.list) if (auto* sym = x->left().getSymbolReference(); isMem(sym)) written.insert(sym);
                    for (auto* sym : written)
                        if (r.syms.count(sym)) {
                            l.count();
                            l.report(p.location, "memory '" + std::string(sym->name) + "' is read and written in one clocked process: read-during-write behaviour (old data, new data, undefined) depends on the target RAM primitive",
                                     {.severity = Severity::Info, .confidence = Confidence::Informational,
                                      .nextStep = "State the intended read-during-write behaviour and check it against the RAM primitive.", .signals = {std::string(sym->name)}});
                        }
                }
            }
            visitDefault(p);
        }
        void handle(const VariableSymbol& v) {
            if (l.isUser(v.location) && isModuleLevel(v) && v.getType().isUnpackedArray() && l.context().design.analysis) {
                l.count();
                std::set<const Symbol*> writers;
                bool comb = false;
                for (auto* d : l.context().design.analysis->getDrivers(v)) {
                    if (d->isInputPort()) continue;
                    writers.insert(d->containingSymbol);
                    comb = comb || d->kind == analysis::DriverKind::Continuous || d->source == analysis::DriverSource::AlwaysComb ||
                           d->source == analysis::DriverSource::AlwaysLatch;
                }
                std::string name(v.name);
                if (writers.size() > 1)
                    l.report(v, "memory '" + name + "' has " + std::to_string(writers.size()) + " write processes: multiple write ports (different clocks make it a dual-clock memory, see CDC-035)",
                             {.confidence = Confidence::Heuristic, .nextStep = "Confirm the target supports this many write ports; otherwise use one write process.", .signals = {name}});
                if (comb)
                    l.report(v, "memory '" + name + "' is written by combinational logic: it infers latches/registers, not a RAM",
                             {.nextStep = "Write memories from a clocked process.", .signals = {name}});
            }
            visitDefault(v);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-054: registers without reset or initial value that steer control flow
class XControlRule final : public LintRule {
public:
    XControlRule() : LintRule("LNT-054") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::set<const Symbol*> unset;                         // registers assigned in clocked code without reset
        std::set<const Symbol*> hasReset;
        std::map<const Symbol*, slang::SourceLocation> use;    // first control use
        explicit V(Lint& lint) : l(lint) {}
        void noteCond(const Expression& e) {
            Reads r;
            e.visit(r);
            for (auto* s : r.syms) use.emplace(s, e.sourceRange.start());
        }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                if (isClocked(p)) {
                    Assigns a;
                    p.getBody().visit(a);
                    bool reset = edgeCount(p) > 1;
                    // synchronous reset pattern: the first statement is an if whose then-branch only assigns constants
                    const Statement* b = &p.getBody();
                    if (b->kind == StatementKind::Timed) b = &b->as<TimedStatement>().stmt;
                    while (b->kind == StatementKind::Block) b = &b->as<BlockStatement>().body;
                    if (b->kind == StatementKind::List && !b->as<StatementList>().list.empty()) b = b->as<StatementList>().list[0];
                    if (b->kind == StatementKind::Conditional) {
                        Assigns t;
                        b->as<ConditionalStatement>().ifTrue.visit(t);
                        bool allConst = !t.list.empty();
                        for (auto* x : t.list) allConst = allConst && l.evalConst(x->right()).isInteger();
                        reset = reset || allConst;
                    }
                    for (auto* x : a.list)
                        if (auto* s = x->left().getSymbolReference(); s && isModuleLevel(*s)) (reset ? hasReset : unset).insert(s);
                }
            }
            visitDefault(p);
        }
        void handle(const ConditionalStatement& c) {
            if (l.isUser(c.sourceRange.start())) for (auto& cond : c.conditions) noteCond(*cond.expr);
            visitDefault(c);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) noteCond(c.expr);
            visitDefault(c);
        }
        void handle(const ConditionalExpression& c) {
            if (l.isUser(c.sourceRange.start())) for (auto& cond : c.conditions) noteCond(*cond.expr);
            visitDefault(c);
        }
    };
    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
        for (auto* s : v.unset) {
            if (v.hasReset.count(s)) continue;
            auto* var = s->as_if<VariableSymbol>();
            if (var && var->getInitializer()) continue;
            auto it = v.use.find(s);
            if (it == v.use.end()) continue;
            l.count();
            l.report(it->second, "register '" + std::string(s->name) + "' has no reset or initial value but steers control flow here: its value is X until first assigned",
                     {.confidence = Confidence::Heuristic, .nextStep = "Reset the register, or guard the control with a valid flag.", .hierarchy = s->getHierarchicalPath(), .signals = {std::string(s->name)}});
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-060: a register assigned with a blocking assignment in a clocked process and read in another process (clocked, combinational or a
// continuous assignment)
class RaceRule final : public LintRule {
public:
    RaceRule() : LintRule("LNT-060", "reads and writes inside the bodies of user subroutines are not followed") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Symbol*, const ProceduralBlockSymbol*> blockingWriter;
        std::map<const Symbol*, std::pair<const ProceduralBlockSymbol*, slang::SourceLocation>> reader;
        std::vector<std::tuple<const Symbol*, const ProceduralBlockSymbol*, slang::SourceLocation>> reads;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && p.procedureKind != ProceduralBlockKind::Initial && p.procedureKind != ProceduralBlockKind::Final) {
                if (isClocked(p)) {
                    Assigns a;
                    p.getBody().visit(a);
                    for (auto* x : a.list)
                        if (!x->isNonBlocking())
                            for (auto* s : lvalueSymbols(x->left()))
                                if (isModuleLevel(*s)) blockingWriter.emplace(s, &p);
                }
                Reads r;
                p.getBody().visit(r);
                for (auto* e : r.refs) reads.emplace_back(e->getSymbolReference(), &p, e->sourceRange.start());
            }
            visitDefault(p);
        }
        // continuous assignments and net initialisers are processes too
        void handle(const ContinuousAssignSymbol& ca) {
            if (l.isUser(ca.location)) {
                Reads r;
                ca.getAssignment().visit(r);
                for (auto* e : r.refs) reads.emplace_back(e->getSymbolReference(), nullptr, e->sourceRange.start());
            }
            visitDefault(ca);
        }
    };
    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
        std::set<const Symbol*> done;
        for (auto& [sym, proc, loc] : v.reads) {
            auto it = v.blockingWriter.find(sym);
            if (it == v.blockingWriter.end() || it->second == proc || !done.insert(sym).second) continue;
            l.count();
            l.report(loc, "'" + std::string(sym->name) + "' is assigned with a blocking assignment in a clocked process and read in another process: the value seen depends on process execution order (race)",
                     {.nextStep = "Use a nonblocking assignment for state shared between clocked processes.", .hierarchy = sym->getHierarchicalPath(), .signals = {std::string(sym->name)}});
        }
    }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-010: unconnected ports. slang reports missing and named-empty connections; the positional empty connection
// `inst(a, , c)` is added here (slang diagnostics stay the primary source).
class UnconnectedRule final : public LintRule {
public:
    UnconnectedRule() : LintRule("LNT-010", "", true) {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
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
                            l.report(pc->sourceRange().start(), "port '" + port + "' of instance '" + std::string(inst.name) + "' is left empty in the positional connection list",
                                     {.nextStep = "Connect the port or tie it off explicitly.", .hierarchy = inst.getHierarchicalPath(), .signals = {port}});
                        }
                        if (pc->kind == syntax::SyntaxKind::OrderedPortConnection || pc->kind == syntax::SyntaxKind::EmptyPortConnection) idx++;
                    }
                }
            }
            visitDefault(inst);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-011: unused nets/variables/parameters/ports come from slang; a deliberate-unused marker silences slang, so the
// marker itself is reported (nothing is suppressed invisibly)
class UnusedRule final : public LintRule {
public:
    UnusedRule() : LintRule("LNT-011", "", true) {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
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
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-009: variables with several drivers are slang's diagnostics. Nets (wire, tri, tri0, tri1, trireg) with overlapping continuous drivers
// are reported here: ERROR-free WARNING Established when none of the drivers can release the net (no all-z arm; x and partly-z arms still drive); when a driver can
// release it (a tri-state bus) the overlap is a conflict unless the enables are provably exclusive, so the finding is WARNING Unresolved
// ("mutual exclusion not proven"), never silence. wand/wor/triand/trior nets resolve overlaps by definition. Instance-output overlaps are
// LNT-042.
// A literal with every bit z releases the net; partial z (8'b0000_zzzz) and x literals still drive bits.
bool allZ(const SVInt& v) {
    if (v.getBitWidth() == 0) return false;
    for (uint32_t i = 0; i < v.getBitWidth(); i++)
        if (v[static_cast<int32_t>(i)].value != slang::logic_t::z.value) return false;
    return true;
}
struct ZSearchV : ASTVisitor<ZSearchV, VisitFlags::AllGood> {
    bool unknown = false;
    void handle(const IntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
    void handle(const UnbasedUnsizedIntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
};
bool ZSearchFn(const Expression& e) {
    ZSearchV z;
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
    auto releases = [](const Expression& arm) { return ZSearchFn(arm); };
    bool aTrueDrives = !releases(ca->left()) && releases(ca->right());
    bool bTrueDrives = !releases(cb->left()) && releases(cb->right());
    if (!aTrueDrives || !bTrueDrives) return false;
    slang::ast::ASTContext ac(l.compilation().getRoot(), slang::ast::LookupLocation::max);
    slang::ast::EvalContext ec(ac);
    return sverity::flow::conditionsExclusive(ec, *ca->conditions[0].expr, true, *cb->conditions[0].expr, true);
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

class NetConflictRule final : public LintRule {
public:
    NetConflictRule() : LintRule("LNT-009", "", true) {}
    struct ZSearch : ASTVisitor<ZSearch, VisitFlags::AllGood> {
        bool unknown = false;
        void handle(const IntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
        void handle(const UnbasedUnsizedIntegerLiteral& e) { unknown = unknown || allZ(e.getValue()); }
    };
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const NetSymbol& n) {
            auto* am = l.context().design.analysis.get();
            auto nk = n.netType.netKind;
            if (am && l.isUser(n.location) && isModuleLevel(n) && (nk == NetType::Wire || nk == NetType::Tri || nk == NetType::Tri0 || nk == NetType::Tri1 || nk == NetType::TriReg)) {
                l.count();
                std::vector<std::pair<std::pair<uint64_t, uint64_t>, const ContinuousAssignSymbol*>> cont;
                for (auto* d : am->getDrivers(n)) {
                    if (d->isInputPort() || d->kind != analysis::DriverKind::Continuous) continue;
                    if (auto* ca = d->containingSymbol->as_if<ContinuousAssignSymbol>()) cont.push_back({d->getBounds(), ca});
                }
                for (size_t i = 0; i < cont.size(); i++)
                    for (size_t j = i + 1; j < cont.size(); j++) {
                        auto [a, b] = std::pair{cont[i], cont[j]};
                        if (a.second == b.second || a.first.second < b.first.first || b.first.second < a.first.first) continue;
                        ZSearch za, zb;
                        a.second->getAssignment().visit(za);
                        b.second->getAssignment().visit(zb);
                        if (constantZ(l, a.second->getAssignment()) || constantZ(l, b.second->getAssignment())) continue;   // a driver that never drives
                        if (za.unknown || zb.unknown) {   // a possible z drive: a tri-state bus; the drivers must never be enabled together
                            if (provablyExclusiveEnables(l, a.second->getAssignment(), b.second->getAssignment())) continue;
                            l.report(b.second->location, "net '" + std::string(n.name) + "' has several overlapping tri-state drivers and their enables are not provably exclusive: a driver conflict cannot be excluded",
                                     {.confidence = Confidence::Unresolved,
                                      .nextStep = "Make the enables mutually exclusive by construction (one select, decoded), or prove it with an assertion.",
                                      .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                            return;
                        }
                        l.report(b.second->location, "net '" + std::string(n.name) + "' has several overlapping continuous drivers and none of them can release it (no z): a driver conflict",
                                 {.nextStep = "Drive the net from one source, or make the drivers tri-state.", .hierarchy = n.getHierarchicalPath(), .signals = {std::string(n.name)}});
                        return;
                    }
            }
            visitDefault(n);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
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
    v.push_back(std::make_unique<MemoryRule>());
    v.push_back(std::make_unique<XControlRule>());
    v.push_back(std::make_unique<RaceRule>());
    return v;
}

} // namespace sverity
