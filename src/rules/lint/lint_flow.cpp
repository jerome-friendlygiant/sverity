// Native AST lint: flow rules. LNT-018 constant conditions, LNT-038 tautological comparisons, LNT-053 overwritten
// assignments, LNT-025/043 overriding conditional assignments, LNT-044 read before assignment, LNT-045 sensitivity.
// Template and helpers: see lint_procedural.cpp and include/sverity/lint.h.
#include <algorithm>
#include <bit>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/numeric/SVInt.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;

// ---------------------------------------------------------------------------------------------------------------
// Shared helpers

// A variable, net or port declared at module scope (instance body, generate block, package or unit).
bool isModuleSignal(const Symbol& s) {
    if (s.kind != SymbolKind::Variable && s.kind != SymbolKind::Net && s.kind != SymbolKind::Port) return false;
    auto* sc = s.getParentScope();
    if (!sc) return false;
    auto k = sc->asSymbol().kind;
    return k == SymbolKind::InstanceBody || k == SymbolKind::GenerateBlock || k == SymbolKind::CompilationUnit ||
           k == SymbolKind::Package;
}

// Value of an integer with no X/Z bits and at most 63 significant bits, so it compares exactly in int64.
std::optional<int64_t> smallValue(const SVInt& v) {
    if (v.hasUnknown() || v.getMinRepresentedBits() > 63) return std::nullopt;
    return v.as<int64_t>();
}

std::optional<int64_t> constSmall(const Lint& l, const Expression& e) {
    auto cv = l.evalConst(e);
    if (!cv.isInteger()) return std::nullopt;
    return smallValue(cv.integer());
}

// Not a compile-time constant (the value depends on a variable, so evaluation fails).
bool isRuntime(const Lint& l, const Expression& e) { return l.evalConst(e).bad(); }

// True when the expression mentions a parameter, genvar or variable: the value depends on the configuration.
struct SignalScan : ASTVisitor<SignalScan, VisitFlags::AllGood> {
    bool found = false;
    void handle(const NamedValueExpression&) { found = true; }
    void handle(const HierarchicalValueExpression&) { found = true; }
};
bool referencesSignal(const Expression& e) {
    SignalScan s;
    e.visit(s);
    return s.found;
}

// An identifier or select chain with no calls or side effects, so both occurrences in one expression see one value.
bool pureRef(const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    switch (e.kind) {
        case ExpressionKind::NamedValue:
        case ExpressionKind::HierarchicalValue:
        case ExpressionKind::IntegerLiteral:
        case ExpressionKind::UnbasedUnsizedIntegerLiteral:
            return true;
        case ExpressionKind::ElementSelect: {
            auto& s = e.as<ElementSelectExpression>();
            return pureRef(s.value()) && pureRef(s.selector());
        }
        case ExpressionKind::RangeSelect: {
            auto& s = e.as<RangeSelectExpression>();
            return pureRef(s.value()) && pureRef(s.left()) && pureRef(s.right());
        }
        case ExpressionKind::MemberAccess:
            return pureRef(e.as<MemberAccessExpression>().value());
        default:
            return false;
    }
}

bool isLiteral01(const Lint& l, const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    if (e.kind != ExpressionKind::IntegerLiteral && e.kind != ExpressionKind::UnbasedUnsizedIntegerLiteral) return false;
    auto v = constSmall(l, e);
    return v && (*v == 0 || *v == 1);
}

// One recorded read or write, in evaluation order (right-hand side before the target of an assignment).
struct Access {
    const ValueSymbol* sym;
    slang::SourceLocation loc;
    bool write;
    bool whole;                       // write to the whole variable (not through a select or member access)
    const AssignmentExpression* asg;  // write: the assignment; read: innermost assignment whose right side holds it
};

// Collects accesses of an expression or statement tree. A call to a user subroutine sets `opaque`: it may read or
// write state that is not visible here, so callers must not reason past it.
struct Collector : ASTVisitor<Collector, VisitFlags::AllGood> {
    std::vector<Access> acc;
    bool opaque = false;
    const AssignmentExpression* owner = nullptr;

    void read(const ValueSymbol& s, slang::SourceLocation loc) { acc.push_back({&s, loc, false, false, owner}); }
    void handle(const NamedValueExpression& n) { read(n.symbol, n.sourceRange.start()); }
    void handle(const HierarchicalValueExpression& n) { read(n.symbol, n.sourceRange.start()); }
    void handle(const CallExpression& c) {
        if (!c.isSystemCall()) opaque = true;
        visitDefault(c);
    }
    void handle(const AssignmentExpression& a) {
        auto* saved = owner;
        owner = &a;
        a.right().visit(*this);
        owner = saved;
        target(a.left(), a, a.isCompound(), true);
    }
    void target(const Expression& e, const AssignmentExpression& a, bool compound, bool whole) {
        switch (e.kind) {
            case ExpressionKind::NamedValue:
            case ExpressionKind::HierarchicalValue: {
                const ValueSymbol& s = e.kind == ExpressionKind::NamedValue
                                           ? e.as<NamedValueExpression>().symbol
                                           : e.as<HierarchicalValueExpression>().symbol;
                if (compound) read(s, e.sourceRange.start());
                acc.push_back({&s, e.sourceRange.start(), true, whole, &a});
                break;
            }
            case ExpressionKind::ElementSelect: {
                auto& s = e.as<ElementSelectExpression>();
                target(s.value(), a, compound, false);
                s.selector().visit(*this);
                break;
            }
            case ExpressionKind::RangeSelect: {
                auto& s = e.as<RangeSelectExpression>();
                target(s.value(), a, compound, false);
                s.left().visit(*this);
                s.right().visit(*this);
                break;
            }
            case ExpressionKind::MemberAccess:
                target(e.as<MemberAccessExpression>().value(), a, compound, false);
                break;
            case ExpressionKind::Concatenation:
                for (auto* op : e.as<ConcatenationExpression>().operands()) target(*op, a, compound, whole);
                break;
            default:
                e.visit(*this);
        }
    }
};

Collector collect(const Statement& s) {
    Collector c;
    s.visit(c);
    return c;
}
Collector collect(const Expression& e) {
    Collector c;
    e.visit(c);
    return c;
}

bool mentions(const Collector& c, const ValueSymbol* s) {
    return std::any_of(c.acc.begin(), c.acc.end(), [&](const Access& a) { return a.sym == s; });
}

// A write that replaces the whole variable: blocking writes always do; nonblocking ones only in combinational code.
bool kills(const Access& a, bool seq) { return a.write && a.whole && (!seq || !a.asg->isNonBlocking()); }

// Statements that run in order with no timing control, loop, call or branch that could observe a variable between
// two of their assignments.
bool straight(const Statement& s) {
    switch (s.kind) {
        case StatementKind::Empty:
        case StatementKind::ExpressionStatement:
        case StatementKind::VariableDeclaration:
            return true;
        case StatementKind::List: {
            auto& l = s.as<StatementList>();
            return std::all_of(l.list.begin(), l.list.end(), [](const Statement* x) { return straight(*x); });
        }
        case StatementKind::Block: {
            auto& b = s.as<BlockStatement>();
            return b.blockKind == StatementBlockKind::Sequential && straight(b.body);
        }
        case StatementKind::Conditional: {
            auto& c = s.as<ConditionalStatement>();
            return straight(c.ifTrue) && (!c.ifFalse || straight(*c.ifFalse));
        }
        case StatementKind::Case: {
            auto& c = s.as<CaseStatement>();
            for (auto& g : c.items)
                if (!straight(*g.stmt)) return false;
            return !c.defaultCase || straight(*c.defaultCase);
        }
        default:
            return false;
    }
}

// Sequential block statements in order, with nested sequential begin/end blocks spliced in.
void flatten(const Statement& s, std::vector<const Statement*>& out, std::unordered_set<const Statement*>& covered) {
    if (s.kind == StatementKind::List) {
        for (auto* x : s.as<StatementList>().list) flatten(*x, out, covered);
    }
    else if (s.kind == StatementKind::Block && s.as<BlockStatement>().blockKind == StatementBlockKind::Sequential) {
        covered.insert(&s);
        flatten(s.as<BlockStatement>().body, out, covered);
    }
    else {
        out.push_back(&s);
    }
}

const AssignmentExpression* topAssign(const Statement& s) {
    if (s.kind != StatementKind::ExpressionStatement) return nullptr;
    auto& e = s.as<ExpressionStatement>().expr;
    return e.kind == ExpressionKind::Assignment ? &e.as<AssignmentExpression>() : nullptr;
}

const ValueSymbol* wholeVar(const Expression& e) {
    return e.kind == ExpressionKind::NamedValue ? &e.as<NamedValueExpression>().symbol : nullptr;
}

// `v = e;` as the only statement of a (possibly begin/end wrapped) body, with v a whole variable.
bool assignedWhole(const Statement& s, const AssignmentExpression*& a, const ValueSymbol*& v) {
    const Statement* x = &s;
    for (;;) {
        if (x->kind == StatementKind::Block && x->as<BlockStatement>().blockKind == StatementBlockKind::Sequential)
            x = &x->as<BlockStatement>().body;
        else if (x->kind == StatementKind::List && x->as<StatementList>().list.size() == 1)
            x = x->as<StatementList>().list[0];
        else
            break;
    }
    auto* asg = topAssign(*x);
    if (!asg || asg->isCompound()) return false;
    auto* sym = wholeVar(asg->left());
    if (!sym || sym->kind != SymbolKind::Variable) return false;
    a = asg;
    v = sym;
    return true;
}

bool singleIf(const ConditionalStatement& c) { return c.conditions.size() == 1 && !c.conditions[0].pattern; }

// Procedure body without its outer event control (always @(...) / always @*).
const Statement& procBody(const ProceduralBlockSymbol& p) {
    const Statement& b = p.getBody();
    return b.kind == StatementKind::Timed ? b.as<TimedStatement>().stmt : b;
}

enum class Style { Comb, Latch, Seq, Level, Other };

// Combinational: always_comb, always @*; Level: always @(a or b) with no edges; Seq: always_ff or edge-controlled.
Style styleOf(const ProceduralBlockSymbol& p) {
    switch (p.procedureKind) {
        case ProceduralBlockKind::AlwaysComb: return Style::Comb;
        case ProceduralBlockKind::AlwaysLatch: return Style::Latch;
        case ProceduralBlockKind::AlwaysFF: return Style::Seq;
        case ProceduralBlockKind::Always: break;
        default: return Style::Other;
    }
    const Statement& body = p.getBody();
    if (body.kind != StatementKind::Timed) return Style::Other;
    const TimingControl& t = body.as<TimedStatement>().timing;
    if (t.kind == TimingControlKind::ImplicitEvent) return Style::Comb;
    std::vector<const TimingControl*> events;
    if (t.kind == TimingControlKind::EventList)
        events.assign(t.as<EventListControl>().events.begin(), t.as<EventListControl>().events.end());
    else
        events.push_back(&t);
    bool edge = false, other = events.empty();
    for (auto* e : events) {
        if (e->kind != TimingControlKind::SignalEvent)
            other = true;
        else if (e->as<SignalEventControl>().edge != EdgeKind::None)
            edge = true;
    }
    if (edge) return Style::Seq;
    return other ? Style::Other : Style::Level;
}

std::string joinNames(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    std::string out;
    for (auto& s : v) out += (out.empty() ? "" : ", ") + s;
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-018: constant if / ?: condition or constant case expression that leaves a branch unreachable.
// Literal-only constant: WARNING. Parameter-dependent: INFO. A lone 0/1 literal in an 'if' (the disable idiom): INFO.
class ConstantConditionRule final : public LintRule {
public:
    ConstantConditionRule()
        : LintRule("LNT-018") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::string hier;
        explicit V(Lint& lint) : l(lint) {}

        void handle(const InstanceBodySymbol& b) {
            auto saved = std::exchange(hier, b.getHierarchicalPath());
            visitDefault(b);
            hier = std::move(saved);
        }
        void emit(slang::SourceLocation at, std::string msg, const Expression& cond, bool isIf) {
            Severity sev = Severity::Warning;
            if (referencesSignal(cond) || (isIf && isLiteral01(l, cond))) sev = Severity::Info;
            l.report(at, std::move(msg),
                     {.severity = sev,
                      .nextStep = "Remove the dead branch, or make the condition depend on a real signal.",
                      .hierarchy = hier});
        }
        // Pattern and multi-condition forms are not modelled: a constant condition there is Unresolved, not skipped.
        template <typename Conds>
        void unmodelled(slang::SourceLocation at, const Conds& conds) {
            for (auto& cond : conds)
                if (!l.evalConst(*cond.expr).bad()) {
                    l.report(at, "constant condition with a pattern or several conditions: not analysed",
                             {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = hier});
                    return;
                }
        }
        void handle(const ConditionalStatement& c) {
            if (l.isUser(c.sourceRange.start()) && !singleIf(c)) unmodelled(c.sourceRange.start(), c.conditions);
            if (singleIf(c) && l.isUser(c.sourceRange.start())) {
                const Expression& cond = *c.conditions[0].expr;
                auto cv = l.evalConst(cond);
                if (!cv.bad()) {
                    l.count();
                    bool taken = cv.isTrue();
                    if (!taken || c.ifFalse)   // a branch is dead unless the else-less condition is always true
                        emit(c.sourceRange.start(),
                             taken ? "constant condition is always true: the else branch is unreachable"
                                   : "constant condition is never true: the then branch is unreachable",
                             cond, true);
                }
            }
            visitDefault(c);
        }
        void handle(const ConditionalExpression& c) {
            if (l.isUser(c.sourceRange.start()) && (c.conditions.size() != 1 || c.conditions[0].pattern))
                unmodelled(c.sourceRange.start(), c.conditions);
            if (c.conditions.size() == 1 && !c.conditions[0].pattern && l.isUser(c.sourceRange.start())) {
                const Expression& cond = *c.conditions[0].expr;
                auto cv = l.evalConst(cond);
                if (!cv.bad()) {
                    l.count();
                    emit(c.sourceRange.start(),
                         cv.isTrue() ? "constant condition selects the true operand: the false operand is unreachable"
                                     : "constant condition never selects the true operand: it is unreachable",
                         cond, false);
                }
            }
            visitDefault(c);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start()) && !l.evalConst(c.expr).bad()) {
                l.count();
                ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
                EvalContext ec(ac);
                auto [taken, known] = c.getKnownBranch(ec);
                if (!known) {
                    l.report(c.sourceRange.start(),
                             "constant case expression with non-constant items: which arms are reachable is not decided",
                             {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = hier});
                }
                else {
                    std::vector<std::string> dead;
                    for (auto& g : c.items)
                        if (g.stmt.get() != taken)
                            dead.push_back("item at line " + std::to_string(l.sm().getLineNumber(g.expressions.front()->sourceRange.start())));
                    if (c.defaultCase && c.defaultCase != taken) dead.push_back("default");
                    if (!dead.empty())
                        emit(c.sourceRange.start(), "constant case expression: unreachable arm(s) " + joinNames(dead), c.expr, false);
                }
            }
            visitDefault(c);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-038: comparisons and logical operators that are the same for every value: same-operand tests (a==a, a<a),
// comparisons with a constant outside the operand's value range (x<0 for unsigned x, 3-bit x==9), and a&&!a, a||!a.
class TautologyRule final : public LintRule {
public:
    TautologyRule()
        : LintRule("LNT-038", "constant-range and same-operand forms only: other Boolean tautologies or contradictions are not proven") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::string hier;
        explicit V(Lint& lint) : l(lint) {}

        void handle(const InstanceBodySymbol& b) {
            auto saved = std::exchange(hier, b.getHierarchicalPath());
            visitDefault(b);
            hier = std::move(saved);
        }
        void handle(const BinaryExpression& b) {
            if (l.isUser(b.sourceRange.start())) {
                switch (b.op) {
                    case BinaryOperator::Equality:
                    case BinaryOperator::Inequality:
                    case BinaryOperator::CaseEquality:
                    case BinaryOperator::CaseInequality:
                    case BinaryOperator::LessThan:
                    case BinaryOperator::LessThanEqual:
                    case BinaryOperator::GreaterThan:
                    case BinaryOperator::GreaterThanEqual:
                        checkCompare(b);
                        break;
                    case BinaryOperator::LogicalAnd:
                    case BinaryOperator::LogicalOr:
                        checkLogical(b);
                        break;
                    default:
                        break;
                }
            }
            visitDefault(b);
        }

        void report(slang::SourceLocation at, std::string msg, const Expression* konst) {
            Severity sev = konst && referencesSignal(*konst) ? Severity::Info : Severity::Warning;
            l.report(at, std::move(msg),
                     {.severity = sev,
                      .nextStep = "A comparison that never changes is usually a typo or a width mistake; fix the operand or the constant.",
                      .hierarchy = hier});
        }

        // Result of "a OP a" for known values.
        static bool sameOperandResult(BinaryOperator op) {
            return op == BinaryOperator::Equality || op == BinaryOperator::CaseEquality ||
                   op == BinaryOperator::LessThanEqual || op == BinaryOperator::GreaterThanEqual;
        }

        // Integer intervals, in the comparison's domain, that a runtime operand of width w can take when it is
        // compared with constant c. False when that comparison is not modelled (negative constant vs unsigned).
        static bool operandDomain(bool opSigned, int64_t w, bool cSigned, int64_t c,
                                  std::vector<std::pair<int64_t, int64_t>>& ivs) {
            if (!opSigned) {   // zero-extended operand; a negative constant would need wraparound
                if (c < 0) return false;
                ivs = {{0, (int64_t(1) << w) - 1}};
                return true;
            }
            if (cSigned) {     // both signed: a signed comparison
                ivs = {{-(int64_t(1) << (w - 1)), (int64_t(1) << (w - 1)) - 1}};
                return true;
            }
            // signed operand against an unsigned constant: sign-extended to the common width, then compared unsigned
            int64_t bits = std::max<int64_t>(w, static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(c))));
            int64_t top = int64_t(1) << bits;
            ivs = {{0, (int64_t(1) << (w - 1)) - 1}, {top - (int64_t(1) << (w - 1)), top - 1}};
            return true;
        }

        // The result of "x OP c" when it is the same for every x in the intervals, nullopt otherwise.
        static std::optional<bool> constantResult(BinaryOperator op, int64_t c,
                                                  const std::vector<std::pair<int64_t, int64_t>>& ivs) {
            if (op == BinaryOperator::Equality || op == BinaryOperator::CaseEquality ||
                op == BinaryOperator::Inequality || op == BinaryOperator::CaseInequality) {
                bool hit = std::any_of(ivs.begin(), ivs.end(), [&](auto& iv) { return iv.first <= c && c <= iv.second; });
                if (hit) return std::nullopt;
                return op == BinaryOperator::Inequality || op == BinaryOperator::CaseInequality;
            }
            // order comparisons are monotone within an interval, so the end points decide
            auto truth = [&](int64_t x) {
                switch (op) {
                    case BinaryOperator::LessThan: return x < c;
                    case BinaryOperator::LessThanEqual: return x <= c;
                    case BinaryOperator::GreaterThan: return x > c;
                    default: return x >= c;
                }
            };
            std::optional<bool> first;
            for (auto& [lo, hi] : ivs)
                for (int64_t x : {lo, hi}) {
                    bool t = truth(x);
                    if (first && *first != t) return std::nullopt;
                    first = t;
                }
            return first;
        }

        void checkCompare(const BinaryExpression& b) {
            const Expression& lhs = b.left().unwrapImplicitConversions();
            const Expression& rhs = b.right().unwrapImplicitConversions();
            if (pureRef(lhs) && pureRef(rhs) && isRuntime(l, lhs) && lhs.isEquivalentTo(rhs)) {
                bool v = sameOperandResult(b.op);
                report(b.sourceRange.start(),
                       std::string("comparison of an operand with itself is always ") + (v ? "true" : "false") + " for known values",
                       nullptr);
                return;
            }
            // operand: a runtime identifier or select; konst: a constant expression
            const Expression* operand;
            const Expression* konst;
            BinaryOperator op = b.op;
            if (pureRef(lhs) && isRuntime(l, lhs) && !isRuntime(l, rhs)) {
                operand = &lhs;
                konst = &rhs;
            }
            else if (pureRef(rhs) && isRuntime(l, rhs) && !isRuntime(l, lhs)) {
                operand = &rhs;
                konst = &lhs;
                op = mirror(op);
            }
            else {
                return;
            }
            auto cv = l.evalConst(*konst);
            if (!cv.isInteger() || cv.integer().hasUnknown()) return;   // real or X/Z constants: other rules
            const Type& ot = *operand->type;
            if (!ot.isIntegral()) return;
            int64_t w = ot.getBitWidth();
            std::vector<std::pair<int64_t, int64_t>> ivs;
            auto c = smallValue(cv.integer());
            if (!c || w == 0 || w > 62 || !operandDomain(ot.isSigned(), w, konst->type->isSigned(), *c, ivs)) {
                l.report(b.sourceRange.start(),
                         "comparison is outside the modelled domain (constant wider than 62 bits, a wide operand, or a negative constant against an unsigned operand): not analysed",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = hier});
                return;
            }
            auto result = constantResult(op, *c, ivs);
            if (!result) return;
            report(b.sourceRange.start(),
                   std::string("comparison of a ") + std::to_string(w) + "-bit " + (ot.isSigned() ? "signed" : "unsigned") +
                       " operand with constant " + std::to_string(*c) + " is always " + (*result ? "true" : "false") +
                       " for known values",
                   konst);
        }

        static BinaryOperator mirror(BinaryOperator op) {
            switch (op) {
                case BinaryOperator::LessThan: return BinaryOperator::GreaterThan;
                case BinaryOperator::GreaterThan: return BinaryOperator::LessThan;
                case BinaryOperator::LessThanEqual: return BinaryOperator::GreaterThanEqual;
                case BinaryOperator::GreaterThanEqual: return BinaryOperator::LessThanEqual;
                default: return op;
            }
        }

        // x is !y for a runtime identifier/select y.
        static bool isNotOf(const Expression& x0, const Expression& y0) {
            const Expression& x = x0.unwrapImplicitConversions();
            if (x.kind != ExpressionKind::UnaryOp || x.as<UnaryExpression>().op != UnaryOperator::LogicalNot) return false;
            const Expression& inner = x.as<UnaryExpression>().operand().unwrapImplicitConversions();
            const Expression& y = y0.unwrapImplicitConversions();
            return pureRef(y) && pureRef(inner) && inner.isEquivalentTo(y);
        }

        void checkLogical(const BinaryExpression& b) {
            const Expression& lhs = b.left();
            const Expression& rhs = b.right();
            bool isAnd = b.op == BinaryOperator::LogicalAnd;
            bool hit = (isNotOf(lhs, rhs) && isRuntime(l, rhs)) || (isNotOf(rhs, lhs) && isRuntime(l, lhs));
            if (!hit) return;
            report(b.sourceRange.start(),
                   isAnd ? "a && !a is always false for known values" : "a || !a is always true for known values", nullptr);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-025 (combinational / blocking) and LNT-043 (sequential / nonblocking): `if (c1) v = e1;` followed later in the
// same procedure by `if (c2) v = e2;` (no else) makes the later assignment win whenever both conditions hold.

// Conditions that cannot both hold: x==c1 with x==c2 (c1 != c2), or x==c with x!=c (x alone is x!=0, !x is x==0).
struct CondForm {
    const Expression* operand;
    bool eq;
    int64_t c;
};
std::optional<CondForm> condForm(const Lint& l, const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
        const Expression& o = e.as<UnaryExpression>().operand().unwrapImplicitConversions();
        if (pureRef(o)) return CondForm{&o, true, 0};
        return std::nullopt;
    }
    if (e.kind == ExpressionKind::BinaryOp) {
        auto& bo = e.as<BinaryExpression>();
        bool eq = bo.op == BinaryOperator::Equality || bo.op == BinaryOperator::CaseEquality;
        bool ne = bo.op == BinaryOperator::Inequality || bo.op == BinaryOperator::CaseInequality;
        if (!eq && !ne) return std::nullopt;
        const Expression& lhs = bo.left().unwrapImplicitConversions();
        const Expression& rhs = bo.right().unwrapImplicitConversions();
        if (pureRef(lhs) && isRuntime(l, lhs))
            if (auto c = constSmall(l, rhs)) return CondForm{&lhs, eq, *c};
        if (pureRef(rhs) && isRuntime(l, rhs))
            if (auto c = constSmall(l, lhs)) return CondForm{&rhs, eq, *c};
        return std::nullopt;
    }
    if (pureRef(e) && isRuntime(l, e)) return CondForm{&e, false, 0};
    return std::nullopt;
}

bool exclusive(const Lint& l, const Expression& e1, const Expression& e2) {
    auto a = condForm(l, e1), b = condForm(l, e2);
    if (!a || !b || !a->operand->isEquivalentTo(*b->operand)) return false;
    return (a->eq && b->eq && a->c != b->c) || (a->eq != b->eq && a->c == b->c);
}

void checkOverrides(Lint& l, const ProceduralBlockSymbol& p, bool nonBlocking) {
    std::vector<const Statement*> list;
    std::unordered_set<const Statement*> covered;
    flatten(procBody(p), list, covered);
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i]->kind != StatementKind::Conditional) continue;
        auto& ci = list[i]->as<ConditionalStatement>();
        const AssignmentExpression* ai;
        const ValueSymbol* v;
        if (!singleIf(ci) || ci.ifFalse || !assignedWhole(ci.ifTrue, ai, v) || ai->isNonBlocking() != nonBlocking) continue;
        for (size_t j = i + 1; j < list.size(); j++) {
            Collector cj = collect(*list[j]);
            if (cj.opaque) break;
            if (list[j]->kind == StatementKind::Conditional) {
                auto& cn = list[j]->as<ConditionalStatement>();
                const AssignmentExpression* aj;
                const ValueSymbol* vj;
                if (singleIf(cn) && !cn.ifFalse && assignedWhole(cn.ifTrue, aj, vj) && vj == v &&
                    aj->isNonBlocking() == nonBlocking) {
                    Collector cc = collect(*cn.conditions[0].expr);
                    if (!mentions(cc, v) && !exclusive(l, *ci.conditions[0].expr, *cn.conditions[0].expr))
                        l.report(list[j]->sourceRange.start(),
                                 "conditional assignment to '" + std::string(v->name) + "' overrides the one on line " +
                                     std::to_string(l.sm().getLineNumber(list[i]->sourceRange.start())) +
                                     " when both conditions hold",
                                 {.confidence = Confidence::Heuristic,
                                  .nextStep = "Use if/else or a priority case so the intended choice is explicit; the conditions may be mutually exclusive.",
                                  .hierarchy = p.getHierarchicalPath(),
                                  .signals = {std::string(v->name)}});
                    break;
                }
            }
            if (mentions(cj, v) || !straight(*list[j])) break;
        }
    }
}

template <bool Seq>
class OverrideRule final : public LintRule {
public:
    OverrideRule()
        : LintRule(Seq ? "LNT-043" : "LNT-025") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            Style st = styleOf(p);
            if (Seq ? st == Style::Seq : (st == Style::Comb || st == Style::Latch || st == Style::Level))
                checkOverrides(l, p, Seq);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-044: a module-level variable read before its first assignment in the same always block, while the block
// assigns it later. Combinational blocks: any whole assignment counts (x = x + 1 is a read before assignment).
// Sequential blocks: only blocking whole assignments count, and the read must precede one in another statement.
void checkReadBeforeAssign(Lint& l, const ProceduralBlockSymbol& p, bool seq) {
    Collector c = collect(procBody(p));
    std::set<const Symbol*> done, killed;
    for (size_t i = 0; i < c.acc.size(); i++) {
        const Access& a = c.acc[i];
        if (!isModuleSignal(*a.sym)) continue;
        if (a.write) {
            if (kills(a, seq)) killed.insert(a.sym);
            continue;
        }
        if (killed.count(a.sym) || done.count(a.sym)) continue;
        bool later = false;
        for (size_t k = i + 1; k < c.acc.size() && !later; k++) {
            const Access& w = c.acc[k];
            later = w.sym == a.sym && kills(w, seq) && (!seq || w.asg != a.asg);
        }
        if (later) {
            done.insert(a.sym);
            l.report(a.loc,
                     "'" + std::string(a.sym->name) + "' is read before its first assignment in this block: the read sees the previous value",
                     {.nextStep = "Assign the variable before reading it, or read it through a separate variable.",
                      .hierarchy = p.getHierarchicalPath(),
                      .signals = {std::string(a.sym->name)}});
        }
    }
}

class ReadBeforeAssignRule final : public LintRule {
public:
    ReadBeforeAssignRule()
        : LintRule("LNT-044") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (!l.isUser(p.location)) return;
            Style st = styleOf(p);
            if (st == Style::Comb) checkReadBeforeAssign(l, p, false);
            else if (st == Style::Seq) checkReadBeforeAssign(l, p, true);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-045: legacy always @(a or b ...) with an explicit level list: a module-level signal read by the body (before
// the body assigns it) but missing from the list is a sensitivity mismatch (WARNING); a listed signal the body never
// reads is INFO. always @*, always_comb and edge-sensitive blocks are not checked.
void checkSensitivity(Lint& l, const ProceduralBlockSymbol& p) {
    const TimingControl& t = p.getBody().as<TimedStatement>().timing;
    std::vector<const TimingControl*> events;
    if (t.kind == TimingControlKind::EventList)
        events.assign(t.as<EventListControl>().events.begin(), t.as<EventListControl>().events.end());
    else
        events.push_back(&t);

    std::set<const Symbol*> listed;
    bool unresolved = false;
    for (auto* e : events) {
        auto* sym = e->as<SignalEventControl>().expr.getSymbolReference();
        if (sym)
            listed.insert(sym);
        else
            unresolved = true;
    }

    Collector c = collect(procBody(p));
    std::set<const Symbol*> killed, external, anyRead;
    for (auto& a : c.acc) {
        if (!isModuleSignal(*a.sym)) continue;
        if (a.write) {
            if (kills(a, true)) killed.insert(a.sym);
        }
        else {
            anyRead.insert(a.sym);
            if (!killed.count(a.sym)) external.insert(a.sym);
        }
    }

    auto minus = [](const std::set<const Symbol*>& a, const std::set<const Symbol*>& b) {
        std::vector<std::string> out;
        for (auto* s : a)
            if (!b.count(s)) out.push_back(std::string(s->name));
        return out;
    };
    auto missing = minus(external, listed);
    auto unused = minus(listed, anyRead);
    std::string where = p.getHierarchicalPath();

    if (!missing.empty())
        l.report(p.location,
                 "always @(...) reads '" + joinNames(missing) + "' but they are not in its sensitivity list: the block does not re-run when they change",
                 {.nextStep = "Add the signals to the list, or use always @* / always_comb.",
                  .hierarchy = where,
                  .signals = missing});
    if (!unused.empty() && !c.opaque)
        l.report(p.location,
                 "sensitivity list entries '" + joinNames(unused) + "' are never read by the block",
                 {.severity = Severity::Info,
                  .nextStep = "Remove unused entries from the list.",
                  .hierarchy = where,
                  .signals = unused});
    if (unresolved)
        l.report(p.location, "sensitivity list entry is not a plain signal: the list is not checked",
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = where});
    if (c.opaque)
        l.report(p.location, "block calls a subroutine: signals read inside it are not checked against the sensitivity list",
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = where});
}

class SensitivityRule final : public LintRule {
public:
    SensitivityRule() : LintRule("LNT-045") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && p.procedureKind == ProceduralBlockKind::Always && styleOf(p) == Style::Level)
                checkSensitivity(l, p);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintFlowRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<ConstantConditionRule>());
    v.push_back(std::make_unique<TautologyRule>());
    v.push_back(std::make_unique<OverrideRule<false>>());
    v.push_back(std::make_unique<OverrideRule<true>>());
    v.push_back(std::make_unique<ReadBeforeAssignRule>());
    v.push_back(std::make_unique<SensitivityRule>());
    return v;
}

} // namespace sverity
