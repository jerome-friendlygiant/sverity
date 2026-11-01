// Native AST lint: synthesizability. LNT-019 non-synthesizable constructs, LNT-061 system tasks, reset and clock-event
// coding (LNT-020 reset coverage, LNT-022 edge mixing, LNT-047 data-driven events, LNT-058 async reset polarity),
// and X/Z hazards (LNT-039 X/Z assignments, LNT-040 X optimism/pessimism). Helpers and template: lint_procedural.cpp.
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "rules/rules.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/statements/MiscStatements.h"
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

const Expression* findXZ(const Expression& e) {
    return findExpr(e, [](const Expression& x) { return isXZ(x); });
}

const Expression* findRef(const Expression& e, const Symbol& s) {
    return findExpr(e, [&s](const Expression& x) {
        return x.kind == ExpressionKind::NamedValue && &x.as<NamedValueExpression>().symbol == &s;
    });
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
bool resetLike(const Symbol* s) {
    auto n = lowerName(s);
    for (auto* k : {"rst", "reset", "clr", "clear"})
        if (n.find(k) != std::string::npos) return true;
    return false;
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
struct ProcWalk : ASTVisitor<ProcWalk<F>, VisitFlags::AllCanonical> {
    F fn;
    explicit ProcWalk(F f) : fn(std::move(f)) {}
    void handle(const ProceduralBlockSymbol& p) { fn(p); }
};

template <typename F>
void forEachProc(Lint& l, F fn) {
    ProcWalk<F> w(std::move(fn));
    l.compilation().getRoot().visit(w);
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
int testPolarity(Lint& l, const Expression& e0, const Symbol& s) {
    const Expression& e = strip(e0);
    if (e.kind == ExpressionKind::NamedValue) return &e.as<NamedValueExpression>().symbol == &s ? 0 : -1;
    if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
        int inner = testPolarity(l, e.as<UnaryExpression>().operand(), s);
        return inner < 0 ? -1 : inner ^ 1;
    }
    if (e.kind == ExpressionKind::BinaryOp) {
        auto& b = e.as<BinaryExpression>();
        if (b.op == BinaryOperator::LogicalOr || b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryAnd) {
            // compound test: the operand that tests `s` decides the polarity (both testing it with different parity -> undecidable)
            int a = testPolarity(l, b.left(), s), c = testPolarity(l, b.right(), s);
            if (a >= 0 && c >= 0 && a != c) return -1;
            return a >= 0 ? a : c;
        }
        if (b.op != BinaryOperator::Equality && b.op != BinaryOperator::Inequality) return -1;
        const Expression& lhs = strip(b.left());
        const Expression& rhs = strip(b.right());
        bool swapped = lhs.kind != ExpressionKind::NamedValue;
        const Expression& ref = swapped ? rhs : lhs;
        const Expression& cst = swapped ? lhs : rhs;
        if (ref.kind != ExpressionKind::NamedValue || &ref.as<NamedValueExpression>().symbol != &s) return -1;
        auto cv = l.evalConst(cst);
        if (!cv.isInteger()) return -1;
        auto c = cv.integer().as<uint64_t>();
        if (!c || *c > 1) return -1;
        bool inverted = b.op == BinaryOperator::Equality ? *c == 0 : *c == 1;
        return inverted ? 1 : 0;
    }
    return -1;
}

struct SeqView {
    const ProceduralBlockSymbol* proc = nullptr;
    const Statement* body = nullptr;                 // statement controlled by the event list
    std::vector<const SignalEventControl*> edges;    // edge-sensitive controls, source order
    std::vector<bool> tested;                        // edge i's signal is referenced by the first if
    const ConditionalStatement* first = nullptr;     // the first statement, when it is an if
    const SignalEventControl* clock = nullptr;
    const SignalEventControl* reset = nullptr;       // asynchronous reset control, when identified
    bool heuristicReset = false;                     // reset chosen by name; the first if does not test it
    bool ambiguous = false;                          // several async controls, or no classification possible
    int polarity = -1;                               // inversions of the reset test in the first if (-1: none)

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
    // Recognition looks at the first if and its else-if chain (the same statement in source terms).
    for (const ConditionalStatement* c = v.first; c; c = asIf(c->ifFalse))
        for (size_t i = 0; i < n; i++) {
            const Symbol* s = symOf(v.edges[i]->expr);
            for (auto& cond : c->conditions)
                if (s && findRef(*cond.expr, *s)) v.tested[i] = true;
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
    }
    if (v.reset && v.first) {
        if (const Symbol* s = symOf(v.reset->expr))
            v.polarity = testPolarity(l, *v.first->conditions[0].expr, *s);
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

// ---------------------------------------------------------------------------------------------------------------
// LNT-019: constructs that have no synthesizable meaning. Final blocks are advisory (INFO); assertions are exempt.
class NonSynthRule final : public LintRule {
public:
    NonSynthRule() : LintRule("LNT-019") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        bool fpga;
        bool inAssert = false;
        bool testbench = false;   // inside a top-level instance without ports: a testbench-like unit, findings are INFO
        std::vector<slang::SourceRange> finals;   // extent of every final block (declarations inside are found by location)
        V(Lint& lint, bool isFpga) : l(lint), fpga(isFpga) {}
        bool inFinal(slang::SourceLocation at) const {
            for (auto& r : finals)
                if (at.buffer() == r.start().buffer() && at.offset() >= r.start().offset() && at.offset() <= r.end().offset())
                    return true;
            return false;
        }

        void emit(slang::SourceLocation at, const std::string& what, Severity base = Severity::Warning,
                  Confidence conf = Confidence::Established) {
            if (inAssert || !l.isUser(at)) return;
            l.count();
            l.report(at, std::string(testbench ? "non-synthesizable construct in a testbench-like top (no ports): " : "non-synthesizable construct in design RTL: ") + what,
                     {.severity = inFinal(at) || testbench ? Severity::Info : base,
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
            emit(e.sourceRange.start(), what + " is not a compile-time constant", Severity::Warning,
                 hasUserCall(e) ? Confidence::Unresolved : Confidence::Established);
        }

        void handle(const InstanceSymbol& inst) {
            bool saved = testbench;
            if (inst.getParentScope() && inst.getParentScope()->asSymbol().kind == SymbolKind::Root) testbench = inst.body.getPortList().empty();
            this->visitDefault(inst);
            testbench = saved;
        }
        void handle(const ProceduralBlockSymbol& p) {
            // initial blocks are initialisation assumptions, reported by LNT-048 (profile aware)
            this->visitDefault(p);
        }
        void handle(const ContinuousAssignSymbol& ca) {
            if (ca.getDelay()) emit(ca.location, "'#' delay on a continuous assignment");
            this->visitDefault(ca);
        }
        void handle(const TimedStatement& t) {
            if (delayCtl(t.timing)) emit(t.sourceRange.start(), "'#' delay");
            this->visitDefault(t);
        }
        void handle(const AssignmentExpression& a) {
            if (a.timingControl && delayCtl(*a.timingControl)) emit(a.sourceRange.start(), "intra-assignment '#' delay");
            this->visitDefault(a);
        }
        void handle(const WaitStatement& s) {
            emit(s.sourceRange.start(), "'wait' statement");
            this->visitDefault(s);
        }
        void handle(const WaitForkStatement& s) {
            emit(s.sourceRange.start(), "'wait fork'");
            this->visitDefault(s);
        }
        void handle(const WaitOrderStatement& s) {
            emit(s.sourceRange.start(), "'wait_order'");
            this->visitDefault(s);
        }
        void handle(const DisableForkStatement& s) {
            emit(s.sourceRange.start(), "'disable fork'");
            this->visitDefault(s);
        }
        void handle(const BlockStatement& b) {
            if (b.blockKind != StatementBlockKind::Sequential) emit(b.sourceRange.start(), "fork/join");
            this->visitDefault(b);
        }
        void handle(const EventTriggerStatement& s) {
            emit(s.sourceRange.start(), "event trigger ('->')");
            this->visitDefault(s);
        }
        void handle(const ProceduralAssignStatement& s) {
            emit(s.sourceRange.start(), s.isForce ? "'force'" : "procedural 'assign'");
            this->visitDefault(s);
        }
        void handle(const ProceduralDeassignStatement& s) {
            emit(s.sourceRange.start(), s.isRelease ? "'release'" : "'deassign'");
            this->visitDefault(s);
        }
        void handle(const ForeverLoopStatement& s) {
            emit(s.sourceRange.start(), "'forever' loop has no static bound");
            this->visitDefault(s);
        }
        void handle(const RepeatLoopStatement& s) {
            checkBound(s.count, false, "'repeat' count");
            this->visitDefault(s);
        }
        void handle(const WhileLoopStatement& s) {
            checkBound(s.cond, false, "'while' condition");
            this->visitDefault(s);
        }
        void handle(const DoWhileLoopStatement& s) {
            checkBound(s.cond, false, "'do-while' condition");
            this->visitDefault(s);
        }
        void handle(const ForLoopStatement& s) {
            if (!s.stopExpr) emit(s.sourceRange.start(), "'for' loop without a condition");
            else checkBound(*s.stopExpr, true, "'for' loop bound");
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
            if (b.getDefinition().definitionKind == DefinitionKind::Program) emit(b.location, "'program' block");
            this->visitDefault(b);
        }
        void handle(const ClassType& c) {
            emit(c.location, "class declaration");
            this->visitDefault(c);
        }
        void handle(const CovergroupType& c) {
            emit(c.location, "covergroup declaration");
            this->visitDefault(c);
        }
        void handle(const VariableSymbol& v) {
            if (auto* sc = v.getParentScope(); sc && sc->asSymbol().kind == SymbolKind::ClassType) return;  // class property
            const Type& t = v.getType();
            const char* what = nullptr;
            if (t.isFloating()) what = "real/shortreal variable";
            else if (t.isString()) what = "string variable";
            else if (t.getCanonicalType().kind == SymbolKind::CHandleType) what = "chandle variable";
            else if (t.isDynamicallySizedArray()) what = "dynamic array variable";
            else if (t.isQueue()) what = "queue variable";
            else if (t.isAssociativeArray()) what = "associative array variable";
            if (what) emit(v.location, std::string(what) + " '" + std::string(v.name) + "'");
            this->visitDefault(v);
        }
    };
    void check(Lint& l) override {
        bool fpga = l.context().config.profile == "fpga";
        V v(l, fpga);
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (p.procedureKind == ProceduralBlockKind::Final)
                v.finals.push_back(slang::SourceRange(p.location, p.getBody().sourceRange.end()));
        });
        l.compilation().getRoot().visit(v);
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
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::set<std::string> allowed;
        bool inFinal = false, inAssert = false;
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
// LNT-020 reset-only registers (assigned only in the reset branch), LNT-067 registers with no reset value (assigned only
// outside the reset branch), LNT-068 always_ff blocks with no reset in a module whose other always_ff blocks reset.
class ResetCodingRule final : public LintRule {
public:
    enum Kind { ResetOnly, NoResetValue, NoResetBlock };
    explicit ResetCodingRule(Kind k) : LintRule(k == ResetOnly ? "LNT-020" : k == NoResetValue ? "LNT-067" : "LNT-068"), kind_(k) {}

private:
    Kind kind_;
    // Bit-precise complement of the statement-level check, from the model's per-bit reset values: bits that no asynchronous reset branch
    // assigns although other bits of the register get a value, and registers of blocks with several asynchronous controls.
    void modelNoResetValue(Lint& l) {
        auto& m = model::getModel(l.context().design);
        struct Sym { std::set<uint32_t> missing, having; const model::Flop* first = nullptr; bool multi = false; };
        std::map<std::string, Sym> syms;
        for (auto& f : m.flops) {
            if (f.resets.empty() || !m.nodes[f.q].sym || !l.isUser(m.nodes[f.q].sym->location)) continue;
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
                       {.nextStep = "Assign the bits in the reset branch.", .hierarchy = m.procs[s.first->proc].instancePath, .signals = {name}});
        }
    }
    struct BodyInfo {
        bool anyReset = false;
        std::vector<const ProceduralBlockSymbol*> noReset;
    };

    void check(Lint& l) override {
        std::map<const InstanceBodySymbol*, BodyInfo> bodies;
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v) return;
            if (v->reset && !v->ambiguous) {
                Branches b = branchesOf(*v);
                if (b.reset) {
                    auto rs = assignsIn(b.reset), os = assignsIn(b.other);
                    std::map<const Symbol*, const AssignmentExpression*> inReset, inOther;
                    for (auto* a : rs)
                        if (auto* s = a->left().getSymbolReference(); s && isModuleLevel(*s)) inReset.emplace(s, a);
                    for (auto* a : os)
                        if (auto* s = a->left().getSymbolReference(); s && isModuleLevel(*s)) inOther.emplace(s, a);
                    auto conf = b.exact ? Confidence::Established : Confidence::Heuristic;
                    for (auto& [s, a] : inReset)
                        if (kind_ == ResetOnly && !inOther.count(s))
                            l.report(a->sourceRange.start(),
                                     "register '" + std::string(s->name) + "' is assigned only in the reset branch (reset-only, never loads)",
                                     {.confidence = conf, .nextStep = "Assign the register in the non-reset branch too.",
                                      .hierarchy = p.getHierarchicalPath(), .signals = {std::string(s->name)}});
                    for (auto& [s, a] : inOther)
                        if (kind_ == NoResetValue && !inReset.count(s))
                            l.report(a->sourceRange.start(),
                                     "register '" + std::string(s->name) + "' has no reset value (assigned only outside the reset branch)",
                                     {.confidence = conf, .nextStep = "Add a reset value for the register.",
                                      .hierarchy = p.getHierarchicalPath(), .signals = {std::string(s->name)}});
                }
            }
            if (kind_ != NoResetBlock || p.procedureKind != ProceduralBlockKind::AlwaysFF) return;
            bool hasReset = v->reset != nullptr || (v->first && [&] {
                for (auto& c : v->first->conditions)
                    if (findExpr(*c.expr, [](const Expression& x) {
                            return x.kind == ExpressionKind::NamedValue && resetLike(&x.as<NamedValueExpression>().symbol);
                        })) return true;
                return false;
            }());
            auto& info = bodies[enclosingBody(p)];
            if (hasReset) info.anyReset = true;
            else if (!assignsIn(v->body).empty()) info.noReset.push_back(&p);
        });
        if (kind_ == NoResetValue) modelNoResetValue(l);
        for (auto& [body, info] : bodies) {
            if (!info.anyReset) continue;
            for (auto* p : info.noReset)
                l.report(*p, "always_ff block has no reset while other always_ff blocks of this module reset their registers",
                         {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                          .nextStep = "Confirm the registers do not need a reset."});
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-022: one signal controls a register on both posedge and negedge.
class BothEdgesRule final : public LintRule {
public:
    BothEdgesRule() : LintRule("LNT-022") {}

private:
    void check(Lint& l) override {
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v || v->edges.size() < 2) return;
            const auto& e = v->edges;
            for (size_t i = 0; i < e.size(); i++)
                for (size_t j = i + 1; j < e.size(); j++) {
                    const Symbol* a = symOf(e[i]->expr);
                    const Symbol* b = symOf(e[j]->expr);
                    if (a && a == b && e[i]->edge != e[j]->edge)
                        l.report(e[j]->expr.sourceRange.start(),
                                 "'" + std::string(a->name) + "' controls the same register on both posedge and negedge",
                                 {.nextStep = "Use one clock edge per register.", .hierarchy = p.getHierarchicalPath(),
                                  .signals = {std::string(a->name)}});
                }
        });
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
            // Distinct controls: key by signal, or by the event itself when the signal is not named.
            std::vector<const void*> keys;
            std::vector<bool> recognised;
            std::vector<std::string> names;
            for (size_t i = 0; i < e.size(); i++) {
                const Symbol* s = symOf(e[i]->expr);
                const void* key = s ? static_cast<const void*>(s) : static_cast<const void*>(e[i]);
                auto it = std::find(keys.begin(), keys.end(), key);
                if (it == keys.end()) {
                    keys.push_back(key);
                    recognised.push_back(v->tested[i]);
                    names.push_back(s ? std::string(s->name) : std::string("<expression>"));
                }
                else {
                    recognised[static_cast<size_t>(it - keys.begin())] = recognised[static_cast<size_t>(it - keys.begin())] || v->tested[i];
                }
            }
            if (keys.size() >= 2) {
                size_t nRec = static_cast<size_t>(std::count(recognised.begin(), recognised.end(), true));
                if (nRec + 1 < keys.size()) {
                    std::string clocks;
                    for (size_t i = 0; i < keys.size(); i++)
                        if (!recognised[i]) clocks += (clocks.empty() ? "" : ", ") + names[i];
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
                std::string rname = std::string(symOf(v->reset->expr) ? symOf(v->reset->expr)->name : std::string_view("<expression>"));
                l.report(p, "async reset '" + rname + "' is not tested by the first if",
                         {.confidence = Confidence::Heuristic, .nextStep = "Test the reset as the first if condition, with the matching polarity.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {rname}});
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-047: edge events (clock or async reset) that are not plain signals (an operator inside the event expression),
// or signals driven by combinational logic (a continuous assign or always_comb whose RHS is not a plain alias).
class DataDrivenEventRule final : public LintRule {
public:
    DataDrivenEventRule() : LintRule("LNT-047") {}

private:
    static bool aliasDriver(const ValueDriver& d, const ValueSymbol& sym) {
        const Symbol& cs = *d.containingSymbol;
        if (cs.kind == SymbolKind::ContinuousAssign) {
            const Expression& a = cs.as<ContinuousAssignSymbol>().getAssignment();
            return a.kind == ExpressionKind::Assignment && isPlain(a.as<AssignmentExpression>().right());
        }
        if (cs.kind != SymbolKind::ProceduralBlock) return true;
        auto as = assignsIn(&cs.as<ProceduralBlockSymbol>().getBody());
        bool any = false;
        for (auto* a : as)
            if (a->left().getSymbolReference() == &sym) {
                any = true;
                if (!isPlain(a->right())) return false;
            }
        return any;
    }

    void check(Lint& l) override {
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (!v) return;
            for (auto* e : v->edges) {
                const Expression& ex = strip(e->expr);
                if (!isPlain(ex)) {
                    l.report(e->expr.sourceRange.start(), "edge event is an expression, not a signal",
                             {.nextStep = "Drive the clock or reset from a port or register.", .hierarchy = p.getHierarchicalPath()});
                    continue;
                }
                auto* sym = symOf(ex);
                auto* vs = sym ? sym->as_if<ValueSymbol>() : nullptr;
                if (!vs) continue;
                auto* an = l.context().design.analysis.get();
                if (!an) {
                    l.report(e->expr.sourceRange.start(), "analysis unavailable: cannot verify that the edge signal is not combinational",
                             {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath()});
                    continue;
                }
                for (const ValueDriver* d : an->getDrivers(*vs)) {
                    if (d->isUnidirectionalPort()) continue;
                    const Symbol& cs = *d->containingSymbol;
                    bool comb = false;
                    if (cs.kind == SymbolKind::ContinuousAssign) comb = true;
                    else if (cs.kind == SymbolKind::ProceduralBlock) {
                        auto& pb = cs.as<ProceduralBlockSymbol>();
                        comb = pb.procedureKind == ProceduralBlockKind::AlwaysComb ||
                               pb.procedureKind == ProceduralBlockKind::AlwaysLatch ||
                               (pb.procedureKind == ProceduralBlockKind::Always && !seqView(l, pb));
                    }
                    if (!comb || aliasDriver(*d, *vs)) continue;
                    l.report(e->expr.sourceRange.start(),
                             "edge event '" + std::string(vs->name) + "' is driven by combinational logic",
                             {.confidence = Confidence::Heuristic, .nextStep = "Clock or reset from a register or a port, not from logic.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {std::string(vs->name)}});
                    break;
                }
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-058 polarity of an asynchronous control must match its edge (also in blocks with several controls); LNT-070 the reset
// branch of an asynchronous reset must assign constants. (A control the first if does not test is LNT-069.)
class AsyncResetControlRule final : public LintRule {
public:
    enum Kind { Polarity, NonConstant };
    explicit AsyncResetControlRule(Kind k) : LintRule(k == Polarity ? "LNT-058" : "LNT-070"), kind_(k) {}

private:
    Kind kind_;
    void check(Lint& l) override {
        forEachProc(l, [&](const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            auto v = seqView(l, p);
            if (v && v->ambiguous && v->edges.size() >= 3) {   // several asynchronous controls (set + reset, power-on + functional)
                size_t nTested = static_cast<size_t>(std::count(v->tested.begin(), v->tested.end(), true));
                if (nTested + 1 != v->edges.size()) return;   // untested controls: LNT-069
                if (kind_ == NonConstant) {   // every branch that tests an asynchronous control assigns constants
                    for (const ConditionalStatement* c = v->first; c; c = asIf(c->ifFalse)) {
                        bool testsControl = false;
                        for (size_t i = 0; i < v->edges.size(); i++) {
                            const Symbol* sy = symOf(v->edges[i]->expr);
                            if (!v->tested[i] || !sy) continue;
                            for (auto& cond : c->conditions) testsControl = testsControl || findRef(*cond.expr, *sy);
                        }
                        if (!testsControl) continue;
                        for (auto* a : assignsIn(&c->ifTrue)) {
                            if (static_cast<bool>(l.evalConst(a->right()))) continue;
                            l.report(a->right().sourceRange.start(), "reset branch assigns a non-constant value to '" +
                                         std::string(a->left().getSymbolReference() ? a->left().getSymbolReference()->name : std::string_view("?")) + "'",
                                     {.severity = Severity::Warning, .confidence = hasUserCall(a->right()) ? Confidence::Unresolved : Confidence::Established,
                                      .nextStep = "Reset values must be constants.", .hierarchy = p.getHierarchicalPath()});
                        }
                    }
                    return;
                }
                for (size_t i = 0; i < v->edges.size(); i++) {
                    if (!v->tested[i]) continue;
                    const Symbol* sy = symOf(v->edges[i]->expr);
                    if (!sy) continue;
                    for (const ConditionalStatement* c = v->first; c; c = asIf(c->ifFalse)) {
                        int pol = -1;
                        for (auto& cond : c->conditions) if (findRef(*cond.expr, *sy)) pol = testPolarity(l, *cond.expr, *sy);
                        if (pol < 0) continue;
                        bool neg = v->edges[i]->edge == EdgeKind::NegEdge;
                        if (((pol & 1) == 1) != neg)
                            l.report(p, std::string(neg ? "negedge" : "posedge") + " '" + std::string(sy->name) + "' but the branch that tests it runs when '" + std::string(sy->name) +
                                            (neg ? "' is high" : "' is low") + ": polarity mismatch",
                                     {.severity = Severity::Error, .confidence = Confidence::Established,
                                      .nextStep = neg ? "Test 'if (!" + std::string(sy->name) + ")' for an active-low control." : "Test 'if (" + std::string(sy->name) + ")' for an active-high control.",
                                      .hierarchy = p.getHierarchicalPath(), .signals = {std::string(sy->name)}});
                        break;
                    }
                }
                return;
            }
            if (!v || v->edges.size() < 2 || v->ambiguous || !v->reset) return;
            std::string rname = std::string(symOf(v->reset->expr) ? symOf(v->reset->expr)->name : std::string_view("<expression>"));
            bool negedge = v->reset->edge == EdgeKind::NegEdge;
            if (v->heuristicReset) return;   // the first if does not test the reset: LNT-069
            if (kind_ == Polarity && v->polarity >= 0 && !v->activeOnTrue()) {
                l.report(p, std::string(negedge ? "negedge" : "posedge") + " '" + rname + "' but the reset branch runs when '" + rname +
                                (negedge ? "' is high" : "' is low") + ": polarity mismatch",
                         {.severity = Severity::Error, .confidence = Confidence::Established,
                          .nextStep = negedge ? "Test 'if (!" + rname + ")' for an active-low reset." : "Test 'if (" + rname + ")' for an active-high reset.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {rname}});
            }
            if (v->polarity >= 0 && !v->activeOnTrue()) return;   // mismatch already reported; the reset branch is unclear
            if (kind_ != NonConstant) return;
            Branches b = branchesOf(*v);
            if (!b.reset || !b.exact) return;
            for (auto* a : assignsIn(b.reset)) {
                if (static_cast<bool>(l.evalConst(a->right()))) continue;
                bool call = hasUserCall(a->right());
                l.report(a->right().sourceRange.start(),
                         "reset branch assigns a non-constant value to '" +
                             std::string(a->left().getSymbolReference() ? a->left().getSymbolReference()->name : std::string_view("?")) + "'",
                         {.severity = Severity::Warning, .confidence = call ? Confidence::Unresolved : Confidence::Established,
                          .nextStep = "Reset values must be constants.", .hierarchy = p.getHierarchicalPath()});
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// X/Z hazards. One visitor serves two rules: LNT-039 (X/Z written into synthesizable values) and LNT-040 (X/Z
// compared, case-matched or used as a condition, and the === family). Final blocks and assertions are INFO.
enum class Nearest { None, Assign, Cond, Compare };

struct XZVisit : ASTVisitor<XZVisit, VisitFlags::AllCanonical> {
    Lint& l;
    bool compare;                         // LNT-040 mode; false = LNT-039 mode
    bool inFinal = false, inAssert = false, inDefault = false;
    Nearest nearest = Nearest::None;      // innermost enclosing assignment / conditional arm / comparison
    const Symbol* lhs = nullptr;          // target of the enclosing assignment or declaration

    XZVisit(Lint& lint, bool cmpMode) : l(lint), compare(cmpMode) {}

    void emit(slang::SourceLocation at, const std::string& msg, Severity base, Confidence conf, std::string nextStep) {
        if (!l.isUser(at)) return;
        l.count();
        l.report(at, msg,
                 {.severity = inFinal || inAssert ? Severity::Info : base, .confidence = conf, .nextStep = std::move(nextStep)});
    }

    // LNT-039: X/Z literal in an assignment or a conditional arm.
    void literal(const Expression& e) {
        if (compare || (nearest != Nearest::Assign && nearest != Nearest::Cond)) return;
        bool hasX, hasZ;
        if (!xzLiteral(e, hasX, hasZ)) return;
        if (inDefault)
            emit(e.sourceRange.start(), "X/Z assigned in a case default (don't-care intent)", Severity::Info, Confidence::Established,
                 "Confirm the default is a don't-care; prefer an explicit value if not.");
        else if (hasZ && !hasX && lhs && lhs->kind == SymbolKind::Net)
            emit(e.sourceRange.start(), "'z drives a net (tri-state)", Severity::Info, Confidence::Established,
                 "Confirm the tri-state is intended.");
        else
            emit(e.sourceRange.start(), "X/Z literal assigned to a synthesizable value", Severity::Warning, Confidence::Established,
                 "Replace the X/Z with a defined value unless a don't-care is intended.");
    }
    // X/Z reaching the assignment through a parameter/localparam, or through a function return value
    void handle(const NamedValueExpression& e) {
        if (!compare && (nearest == Nearest::Assign || nearest == Nearest::Cond) && e.symbol.kind == SymbolKind::Parameter) {
            auto cv = e.symbol.as<ParameterSymbol>().getValue();
            if (cv.isInteger() && cv.integer().hasUnknown())
                emit(e.sourceRange.start(), "parameter '" + std::string(e.symbol.name) + "' with X/Z bits is assigned to a synthesizable value", Severity::Warning,
                     Confidence::Established, "Replace the X/Z in the parameter with a defined value unless a don't-care is intended.");
        }
    }
    void handle(const ReturnStatement& r) {
        if (r.expr) { Scoped<Nearest> n(nearest, Nearest::Assign); r.expr->visit(*this); }
    }
    void handle(const UnbasedUnsizedIntegerLiteral& e) { literal(e); }
    void handle(const IntegerLiteral& e) { literal(e); }

    void handle(const AssignmentExpression& a) {
        { Scoped<Nearest> n(nearest, Nearest::None); a.left().visit(*this); }
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<const Symbol*> s(lhs, a.left().getSymbolReference());
        a.right().visit(*this);
    }
    void handle(const ContinuousAssignSymbol& ca) {
        Scoped<Nearest> n(nearest, Nearest::None);
        ca.getAssignment().visit(*this);
    }
    void handle(const VariableSymbol& v) {
        const Expression* init = v.getDeclaredType() ? v.getDeclaredType()->getInitializer() : nullptr;
        if (!init) return;
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<const Symbol*> s(lhs, &v);
        init->visit(*this);
    }
    void handle(const NetSymbol& v) {
        const Expression* init = v.getDeclaredType() ? v.getDeclaredType()->getInitializer() : nullptr;
        if (!init) return;
        Scoped<Nearest> n(nearest, Nearest::Assign);
        Scoped<const Symbol*> s(lhs, &v);
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

    void handle(const ConditionalExpression& c) {
        if (compare)
            for (auto& cond : c.conditions)
                if (isXZ(*cond.expr) && oneBitLiteral(strip(*cond.expr)))
                    emit(cond.expr->sourceRange.start(), "X/Z literal selects a conditional operator: result is X in simulation",
                         Severity::Warning, Confidence::Established, "Use a defined select value.");
        for (auto& cond : c.conditions) cond.expr->visit(*this);
        Scoped<Nearest> n(nearest, nearest == Nearest::Compare ? Nearest::Compare : Nearest::Cond);
        c.left().visit(*this);
        c.right().visit(*this);
    }

    void handle(const BinaryExpression& b) {
        bool cmp = b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality ||
                   b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality ||
                   b.op == BinaryOperator::WildcardEquality || b.op == BinaryOperator::WildcardInequality;
        if (compare) {
            if (b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality)
                emit(b.sourceRange.start(), "'===' / '!==' are not synthesizable (4-state equality)", Severity::Warning,
                     Confidence::Established, "Use '==' / '!=' on defined values.");
            else if ((b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality) &&
                     (findXZ(b.left()) || findXZ(b.right())))
                emit(b.sourceRange.start(),
                     "comparison with an X/Z literal: the result is X (never true in simulation) and synthesis ignores it",
                     Severity::Warning, Confidence::Established, "Compare against a defined value, or use casez with '?' items.");
        }
        Scoped<Nearest> n(nearest, cmp ? Nearest::Compare : nearest);
        this->visitDefault(b);
    }

    void handle(const InsideExpression& e) {
        bool hit = compare && findXZ(e.left());
        for (auto* r : e.rangeList()) hit = hit || (compare && findXZ(*r));
        if (hit)
            emit(e.sourceRange.start(), "'inside' with an X/Z literal: the match is X in simulation and ignored by synthesis",
                 Severity::Warning, Confidence::Established, "Use defined values in the set.");
        Scoped<Nearest> n(nearest, Nearest::Compare);
        this->visitDefault(e);
    }

    void handle(const CaseStatement& c) {
        { Scoped<Nearest> n(nearest, Nearest::None); c.expr.visit(*this); }
        for (auto& g : c.items) {
            if (compare && c.condition == CaseStatementCondition::Normal)
                for (auto* e : g.expressions)
                    if (findXZ(*e))
                        emit(e->sourceRange.start(), "case item with an X/Z literal never matches in simulation and is ignored by synthesis",
                             Severity::Warning, Confidence::Established, "Use casez/casex with '?' for don't-care bits, or a defined value.");
            Scoped<Nearest> n(nearest, Nearest::Compare);
            for (auto* e : g.expressions) e->visit(*this);
            g.stmt->visit(*this);
        }
        if (c.defaultCase) {
            Scoped<bool> d(inDefault, true);
            c.defaultCase->visit(*this);
        }
    }

    void handle(const ConditionalStatement& s) {
        if (compare)
            for (auto& c : s.conditions)
                if (isXZ(*c.expr) && oneBitLiteral(strip(*c.expr)))
                    emit(c.expr->sourceRange.start(), "if condition is an X/Z literal: never taken in simulation and removed by synthesis",
                         Severity::Warning, Confidence::Established, "Use a defined condition.");
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
    v.push_back(std::make_unique<NonSynthRule>());
    v.push_back(std::make_unique<SystemTaskRule>());
    for (auto k : {ResetCodingRule::ResetOnly, ResetCodingRule::NoResetValue, ResetCodingRule::NoResetBlock})
        v.push_back(std::make_unique<ResetCodingRule>(k));
    v.push_back(std::make_unique<BothEdgesRule>());
    v.push_back(std::make_unique<UntestedEdgeControlRule>());
    v.push_back(std::make_unique<DataDrivenEventRule>());
    for (auto k : {AsyncResetControlRule::Polarity, AsyncResetControlRule::NonConstant})
        v.push_back(std::make_unique<AsyncResetControlRule>(k));
    v.push_back(std::make_unique<XZAssignRule>());
    v.push_back(std::make_unique<XZCompareRule>());
    return v;
}

} // namespace sverity
