// Native lint on the shared model and the AST: clock/reset coupling (LNT-027), FSM reachability obligations (LNT-029),
// initialisation assumptions (LNT-048), memories (LNT-049), inferred enables (LNT-052), X-sensitive control (LNT-054)
// and sequential races (LNT-060).
#include <map>
#include <set>

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
// LNT-027: a clock net used as data, or a reset net steering a clock net
class CouplingRule final : public LintRule {
public:
    CouplingRule() : LintRule("LNT-027", "clock-as-data and reset-gated clocks only; 'control' signals other than clocks and resets are not classified") {}
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
// reported, reachability and liveness are always emitted as proof obligations (never proven here).
class FsmRule final : public LintRule {
public:
    FsmRule() : LintRule("LNT-029") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Type*, std::set<uint64_t>> assigned;      // constants ever assigned to a variable of the enum type
        std::map<const Type*, bool> nonConstAssign, clocked;
        std::map<const Type*, const Symbol*> example;
        std::vector<const CaseStatement*> cases;
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
            visitDefault(p);
        }
        void handle(const VariableSymbol& v) {
            if (l.isUser(v.location) && v.getInitializer() && isModuleLevel(v)) noteAssign(*v.getInitializer(), *v.getInitializer(), false);   // initialiser value
            visitDefault(v);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) cases.push_back(&c);
            visitDefault(c);
        }
    };
    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
        std::map<const Type*, const CaseStatement*> fsmCase;
        for (auto* c : v.cases) if (auto* t = V::enumTypeOf(c->expr)) fsmCase.emplace(t, c);
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
                l.report(c->sourceRange.start(), "FSM state(s) [" + dead + "] are never assigned: unreachable unless they are the power-up value",
                         {.confidence = Confidence::Heuristic, .nextStep = "Remove the state or add the missing transition.", .signals = {dead}});
            // stuck-state candidates: a case arm that selects one state and contains no assignment to a state variable at all
            for (auto& g : c->items) {
                Assigns a;
                g.stmt->visit(a);
                bool any = false;
                for (auto* x : a.list) any = any || V::enumTypeOf(x->left()) == t;
                if (!any && !c->defaultCase && g.expressions.size() == 1 && g.stmt->kind != StatementKind::Empty)
                    l.report(g.expressions[0]->sourceRange.start(), "FSM state arm has no transition: the state can never be left unless a default assignment precedes the case",
                             {.confidence = Confidence::Heuristic, .nextStep = "Add the exit transition, or make the hold explicit.", .signals = {}});
            }
            l.report(c->sourceRange.start(), "FSM over states [" + all + "]: reachability of every state and absence of deadlock are not proven",
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
                                     {.severity = Severity::Info, .confidence = Confidence::Informational, .proof = ProofState::NotAttempted,
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
// LNT-052: inferred clock enables and priority selection chains in clocked blocks
class EnableRule final : public LintRule {
public:
    EnableRule() : LintRule("LNT-052") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        static const Symbol* sole(const Statement& s) {
            Assigns a;
            s.visit(a);
            const Symbol* t = nullptr;
            for (auto* x : a.list) {
                auto* sy = x->left().getSymbolReference();
                if (!sy || !isModuleLevel(*sy)) continue;
                if (t && t != sy) return nullptr;
                t = sy;
            }
            return t;
        }
        void chain(const ConditionalStatement& c) {
            int arms = 1;
            const Statement* next = c.ifFalse;
            const Symbol* target = sole(c.ifTrue);
            while (next && next->kind == StatementKind::Conditional) {
                auto& n = next->as<ConditionalStatement>();
                if (sole(n.ifTrue) != target) { target = nullptr; break; }
                arms++;
                next = n.ifFalse;
            }
            if (target && arms >= 3)
                l.report(c.sourceRange.start(), "else-if chain of " + std::to_string(arms) + " arms assigns '" + std::string(target->name) + "': infers a priority multiplexer",
                         {.severity = Severity::Info, .confidence = Confidence::Informational, .nextStep = "Use a case statement (and unique) if the conditions are mutually exclusive.", .signals = {std::string(target->name)}});
        }
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && isClocked(p)) {
                l.count();
                bool asyncReset = edgeCount(p) > 1;
                const Statement* body = &p.getBody();
                if (body->kind == StatementKind::Timed) body = &body->as<TimedStatement>().stmt;
                while (body->kind == StatementKind::Block) body = &body->as<BlockStatement>().body;
                std::vector<const Statement*> top;
                if (body->kind == StatementKind::List) for (auto* s : body->as<StatementList>().list) top.push_back(s); else top.push_back(body);
                for (size_t i = 0; i < top.size(); i++) {
                    const Statement* s = top[i];
                    if (s->kind != StatementKind::Conditional) continue;
                    auto& c = s->as<ConditionalStatement>();
                    chain(c);
                    // `if (en) q <= d;` (no else): clock enable. With an asynchronous reset, the reset if/else wraps the enable.
                    const ConditionalStatement* en = &c;
                    if (asyncReset && c.ifFalse && c.ifFalse->kind == StatementKind::Conditional) en = &c.ifFalse->as<ConditionalStatement>();
                    else if (asyncReset) continue;
                    if (!en->ifFalse && sole(en->ifTrue))
                        l.report(en->sourceRange.start(), "register '" + std::string(sole(en->ifTrue)->name) + "' is only assigned under a condition: a clock enable is inferred",
                                 {.severity = Severity::Info, .confidence = Confidence::Informational, .nextStep = "Intended enables are fine; otherwise add the else branch.", .signals = {std::string(sole(en->ifTrue)->name)}});
                }
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-054: registers without reset or initial value that steer control flow
class XControlRule final : public LintRule {
public:
    XControlRule() : LintRule("LNT-054", "reset/initialisation reachability is judged per register (no reset event, no initialiser); sequencing and reset-by-construction in other processes are not analysed") {}
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
// LNT-060: a register assigned with a blocking assignment in one clocked process and read in another one
class RaceRule final : public LintRule {
public:
    RaceRule() : LintRule("LNT-060", "blocking-assignment races between clocked processes of one clock; races through subroutines, hierarchical references and with combinational processes are not analysed") {}
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Symbol*, const ProceduralBlockSymbol*> blockingWriter;
        std::map<const Symbol*, std::pair<const ProceduralBlockSymbol*, slang::SourceLocation>> reader;
        std::vector<std::tuple<const Symbol*, const ProceduralBlockSymbol*, slang::SourceLocation>> reads;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && isClocked(p)) {
                Assigns a;
                p.getBody().visit(a);
                for (auto* x : a.list)
                    if (auto* s = x->left().getSymbolReference(); s && isModuleLevel(*s) && !x->isNonBlocking()) blockingWriter.emplace(s, &p);
                Reads r;
                p.getBody().visit(r);
                for (auto* e : r.refs) reads.emplace_back(e->getSymbolReference(), &p, e->sourceRange.start());
            }
            visitDefault(p);
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
            l.report(loc, "'" + std::string(sym->name) + "' is assigned with a blocking assignment in one clocked process and read in another: the value seen depends on process execution order (race)",
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
// LNT-013: latches inferred by legacy level-sensitive `always` blocks (always_comb is slang's inferred-latch diagnostic).
// A module-level variable assigned somewhere in the block but not on every path (if without else, case without default).
class LegacyLatchRule final : public LintRule {
public:
    LegacyLatchRule() : LintRule("LNT-013", "", true) {}
    using Set = std::set<const Symbol*>;
    static Set definitely(const Statement& st) {
        Set out;
        switch (st.kind) {
            case StatementKind::ExpressionStatement: {
                auto& e = st.as<ExpressionStatement>().expr;
                if (e.kind == ExpressionKind::Assignment) {
                    auto& a = e.as<AssignmentExpression>();
                    if (a.left().kind == ExpressionKind::NamedValue)
                        if (auto* sym = a.left().getSymbolReference()) out.insert(sym);
                }
                break;
            }
            case StatementKind::List:
                for (auto* x : st.as<StatementList>().list) { Set t = definitely(*x); out.insert(t.begin(), t.end()); }
                break;
            case StatementKind::Block: out = definitely(st.as<BlockStatement>().body); break;
            case StatementKind::Timed: out = definitely(st.as<TimedStatement>().stmt); break;
            case StatementKind::Conditional: {
                auto& c = st.as<ConditionalStatement>();
                if (c.ifFalse) {
                    Set a = definitely(c.ifTrue), b = definitely(*c.ifFalse);
                    for (auto* x : a) if (b.count(x)) out.insert(x);
                }
                break;
            }
            case StatementKind::Case: {
                auto& c = st.as<CaseStatement>();
                if (!c.defaultCase) break;   // not provably exhaustive (slang's case-incomplete covers exhaustive coverage)
                out = definitely(*c.defaultCase);
                for (auto& g : c.items) {
                    Set t = definitely(*g.stmt), r;
                    for (auto* x : out) if (t.count(x)) r.insert(x);
                    out = std::move(r);
                }
                break;
            }
            default: break;
        }
        return out;
    }
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            const Statement& body = p.getBody();
            bool legacyComb = p.procedureKind == ProceduralBlockKind::Always && !isClocked(p) && body.kind == StatementKind::Timed;
            if (legacyComb && l.isUser(p.location)) {
                l.count();
                Assigns a;
                body.visit(a);
                Set all;
                for (auto* x : a.list)
                    if (auto* sym = x->left().getSymbolReference(); sym && isModuleLevel(*sym)) all.insert(sym);
                Set done = definitely(body);
                std::string names;
                for (auto* sym : all) if (!done.count(sym)) names += (names.empty() ? "" : ", ") + std::string(sym->name);
                if (!names.empty())
                    l.report(p.location, "level-sensitive always block does not assign [" + names + "] on every path: a latch is inferred",
                             {.confidence = Confidence::Heuristic, .nextStep = "Assign a default at the top of the block, add the else/default branch, or use always_comb to get slang's check.", .hierarchy = p.getHierarchicalPath(), .signals = {names}});
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-053: a whole-variable assignment that is overwritten before it is read. Path-aware walk over if/case/loops: a pending
// (unread) write is overwritten by a later whole-variable write that dominates it (the overwriting statement sits in the same
// or an enclosing statement list, so `q <= 0; if (en) q <= d;` is the default-then-override idiom and is not reported).
class DeadStoreRule final : public LintRule {
public:
    DeadStoreRule() : LintRule("LNT-053") {}
    struct W { slang::SourceLocation loc; int frame; bool uncertain; };
    using State = std::map<const Symbol*, std::vector<W>>;
    struct Walker {
        Lint& l;
        std::vector<int> parent{-1};   // frame tree
        std::set<std::pair<uint32_t, size_t>> reported;
        explicit Walker(Lint& lint) : l(lint) {}
        int newFrame(int p) { parent.push_back(p); return static_cast<int>(parent.size()) - 1; }
        bool dominates(int anc, int f) const { for (; f >= 0; f = parent[f]) if (f == anc) return true; return false; }
        static void merge(State& into, const State& o) {
            for (auto& [s, ws] : o) for (auto& w : ws) {
                auto& v = into[s];
                bool dup = false;
                for (auto& x : v) dup = dup || (x.loc == w.loc);
                if (!dup) v.push_back(w);
            }
        }
        struct Info : ASTVisitor<Info, VisitFlags::AllGood> {
            std::set<const Symbol*> reads, writes;
            bool call = false, timing = false;
            void handle(const NamedValueExpression& e) { if (auto* s = e.getSymbolReference()) reads.insert(s); }
            void handle(const AssignmentExpression& a) {
                if (a.left().kind == ExpressionKind::NamedValue) { if (auto* s = a.left().getSymbolReference()) writes.insert(s); }
                else a.left().visit(*this);
                if (a.isCompound() && a.left().kind == ExpressionKind::NamedValue) a.left().visit(*this);
                a.right().visit(*this);
                if (a.timingControl) timing = true;
            }
            void handle(const CallExpression& c) {
                if (!c.isSystemCall()) call = true;
                visitDefault(c);
            }
            void handle(const TimedStatement& t) { timing = true; visitDefault(t); }
        };
        void readAll(State& st, const std::set<const Symbol*>& syms) { for (auto* s : syms) st.erase(s); }
        void apply(State& st, const Info& in) {
            if (in.timing) { st.clear(); return; }
            readAll(st, in.reads);
            if (in.call) for (auto& [s, ws] : st) for (auto& w : ws) w.uncertain = true;
        }
        void report(const W& old, const Symbol& sym, bool sameFrame) {
            if (!reported.insert({static_cast<uint32_t>(old.loc.offset()), old.loc.buffer().getId()}).second) return;
            l.report(old.loc, "assignment to '" + std::string(sym.name) + "' is overwritten before it is read" + (sameFrame ? "" : " (on at least one path)"),
                     {.confidence = old.uncertain ? Confidence::Unresolved : (sameFrame ? Confidence::Established : Confidence::Heuristic),
                      .nextStep = old.uncertain ? "A subroutine call lies between the two writes and may read the value; review it." : "Remove the first assignment or use the value.",
                      .signals = {std::string(sym.name)}});
        }
        void walk(const Statement& st, int frame, State& state) {
            switch (st.kind) {
                case StatementKind::List: {
                    int f = newFrame(frame);
                    for (auto* x : st.as<StatementList>().list) walk(*x, f, state);
                    return;
                }
                case StatementKind::Block: walk(st.as<BlockStatement>().body, frame, state); return;
                case StatementKind::Timed: { state.clear(); walk(st.as<TimedStatement>().stmt, frame, state); state.clear(); return; }
                case StatementKind::ExpressionStatement: {
                    auto& e = st.as<ExpressionStatement>().expr;
                    Info in;
                    if (e.kind == ExpressionKind::Assignment && e.as<AssignmentExpression>().left().kind == ExpressionKind::NamedValue) {
                        auto& a = e.as<AssignmentExpression>();
                        Info rhs;
                        a.right().visit(rhs);
                        if (a.isCompound()) a.left().visit(rhs);
                        apply(state, rhs);
                        auto* sym = a.left().getSymbolReference();
                        if (sym && !a.isCompound() && !a.timingControl) {
                            auto it = state.find(sym);
                            if (it != state.end()) {
                                for (auto& w : it->second) if (dominates(frame, w.frame)) report(w, *sym, w.frame == frame);
                                it->second.erase(std::remove_if(it->second.begin(), it->second.end(), [&](const W& w) { return dominates(frame, w.frame); }), it->second.end());
                            }
                            state[sym].push_back({a.sourceRange.start(), frame, false});
                        } else if (sym) state.erase(sym);
                        return;
                    }
                    e.visit(in);
                    apply(state, in);
                    for (auto* s : in.writes) state.erase(s);
                    return;
                }
                case StatementKind::Conditional: {
                    auto& c = st.as<ConditionalStatement>();
                    Info in;
                    for (auto& cond : c.conditions) cond.expr->visit(in);
                    apply(state, in);
                    State a = state, b = state;
                    int fa = newFrame(frame), fb = newFrame(frame);
                    walk(c.ifTrue, fa, a);
                    if (c.ifFalse) walk(*c.ifFalse, fb, b);
                    state = a;
                    merge(state, b);
                    return;
                }
                case StatementKind::Case: {
                    auto& c = st.as<CaseStatement>();
                    Info in;
                    c.expr.visit(in);
                    for (auto& g : c.items) for (auto* e : g.expressions) e->visit(in);
                    apply(state, in);
                    State out;
                    bool first = true;
                    for (auto& g : c.items) { State s = state; walk(*g.stmt, newFrame(frame), s); if (first) out = s; else merge(out, s); first = false; }
                    State d = state;
                    if (c.defaultCase) walk(*c.defaultCase, newFrame(frame), d);
                    if (first) out = d; else merge(out, d);
                    state = out;
                    return;
                }
                default: {
                    Info in;
                    st.visit(in);
                    if (in.timing) { state.clear(); return; }
                    // loops and anything else: reads/writes inside are not ordered here; inner straight-line code is checked on its own
                    readAll(state, in.reads);
                    for (auto* s : in.writes) state.erase(s);
                    if (in.call) for (auto& [s, ws] : state) for (auto& w : ws) w.uncertain = true;
                    return;
                }
            }
        }
    };
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (p.procedureKind != ProceduralBlockKind::Final && l.isUser(p.location)) {
                l.count();
                Walker w(l);
                State st;
                w.walk(p.getBody(), 0, st);
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ----------------------------------------------------------------------------------------------------------------
// LNT-009: variables with several drivers are slang's diagnostics; resolved nets (wire) with overlapping continuous drivers
// none of which can release the net (no z) are reported here. Instance-output overlaps are LNT-042.
class NetConflictRule final : public LintRule {
public:
    NetConflictRule() : LintRule("LNT-009", "", true) {}
    struct ZSearch : ASTVisitor<ZSearch, VisitFlags::AllGood> {
        bool unknown = false;
        void handle(const IntegerLiteral& e) { unknown = unknown || e.getValue().hasUnknown(); }
        void handle(const UnbasedUnsizedIntegerLiteral& e) { unknown = unknown || e.getValue().hasUnknown(); }
    };
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const NetSymbol& n) {
            auto* am = l.context().design.analysis.get();
            if (am && l.isUser(n.location) && isModuleLevel(n) && n.netType.netKind == NetType::Wire) {
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
                        if (za.unknown || zb.unknown) continue;   // a possible z drive is a tri-state bus (not a conflict)
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
    v.push_back(std::make_unique<DeadStoreRule>());
    v.push_back(std::make_unique<LegacyLatchRule>());
    v.push_back(std::make_unique<CouplingRule>());
    v.push_back(std::make_unique<FsmRule>());
    v.push_back(std::make_unique<InitRule>());
    v.push_back(std::make_unique<MemoryRule>());
    v.push_back(std::make_unique<EnableRule>());
    v.push_back(std::make_unique<XControlRule>());
    v.push_back(std::make_unique<RaceRule>());
    return v;
}

} // namespace sverity
