// Native AST lint: flow rules. LNT-018 constant conditions, LNT-038 tautological comparisons, LNT-053 overwritten
// assignments, LNT-025/043 overriding conditional assignments, LNT-044 read before assignment, LNT-045 sensitivity.
// Template and helpers: see lint_procedural.cpp and include/sverity/lint.h.
#include <algorithm>
#include <bit>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analysis/flow.h"
#include "rules/lint/value_range.h"
#include "slang/ast/ValuePath.h"
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
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/numeric/SVInt.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;
namespace fl = sverity::flow;

// ---------------------------------------------------------------------------------------------------------------
// Shared helpers

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

using fl::isPureRef;
bool pureRef(const Expression& e) { return isPureRef(e); }

bool isLiteral01(const Lint& l, const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    if (e.kind != ExpressionKind::IntegerLiteral && e.kind != ExpressionKind::UnbasedUnsizedIntegerLiteral) return false;
    auto v = constSmall(l, e);
    return v && (*v == 0 || *v == 1);
}

bool singleIf(const ConditionalStatement& c) { return c.conditions.size() == 1 && !c.conditions[0].pattern; }

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
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
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
                if (!cv.bad() && (cv.isTrue() || cv.isFalse())) {
                    l.count();
                    emit(c.sourceRange.start(),
                         cv.isTrue() ? "constant condition selects the true operand: the false operand is unreachable"
                                     : "constant condition never selects the true operand: it is unreachable",
                         cond, false);
                }
            }
            visitDefault(c);
        }
        // A case item that is a runtime value (or a range with a runtime bound): whether it matches is decided at run
        // time, so it claims nothing about reachability.
        static bool runtimeItem(const Lint& l, const Expression& e) {
            if (e.kind == ExpressionKind::ValueRange) {
                const auto& r = e.as<ValueRangeExpression>();
                return isRuntime(l, r.left()) || isRuntime(l, r.right());
            }
            return isRuntime(l, e);
        }

        // Whether a constant item matches a constant case expression, as slang's checkMatch does. Nullopt: not modelled
        // (a non-integer value, or an inside-form match).
        static std::optional<bool> constantMatch(CaseStatementCondition cond, const ConstantValue& expr,
                                                 const ConstantValue& item) {
            if (!expr.isInteger() || !item.isInteger() || cond == CaseStatementCondition::Inside) return std::nullopt;
            if (cond == CaseStatementCondition::Normal) return expr == item;
            if (cond == CaseStatementCondition::WildcardJustZ) return caseZWildcardEqual(expr.integer(), item.integer());
            return caseXWildcardEqual(expr.integer(), item.integer());
        }

        // A constant case expression with at least one runtime item. Items are tested in order, as the case does. A
        // constant item that matches always matches when it is reached, so every later item and the default are
        // unreachable. A constant item that can never match is unreachable at that item. A runtime item may or may not
        // match, so it claims nothing.
        void checkMixedCase(const CaseStatement& c) {
            const ConstantValue expr = l.evalConst(c.expr);
            std::optional<size_t> definite;   // index of the group holding the first matching constant item
            for (size_t gi = 0; gi < c.items.size() && !definite; gi++) {
                for (const Expression* e : c.items[gi].expressions) {
                    if (runtimeItem(l, *e)) continue;
                    std::optional<bool> m;   // a constant range is not modelled
                    if (e->kind != ExpressionKind::ValueRange) m = constantMatch(c.condition, expr, l.evalConst(*e));
                    if (!m) {
                        l.report(c.sourceRange.start(),
                                 "constant case expression with a constant range, inside-form or non-integer item: which arms are reachable is not decided",
                                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = hier});
                        return;
                    }
                    if (*m) {
                        definite = gi;
                        break;   // later items of this group are not listed: the arm is listed as a whole, as before
                    }
                    emit(e->sourceRange.start(), "constant case item can never equal the constant case expression: unreachable item",
                         c.expr, false);
                }
            }
            if (!definite) return;
            std::vector<std::string> dead;
            for (size_t gi = *definite + 1; gi < c.items.size(); gi++)
                dead.push_back("item at line " + std::to_string(l.sm().getLineNumber(c.items[gi].expressions.front()->sourceRange.start())));
            if (c.defaultCase) dead.push_back("default");
            if (!dead.empty())
                emit(c.sourceRange.start(), "constant case expression: unreachable arm(s) " + joinNames(dead), c.expr, false);
        }

        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start()) && !l.evalConst(c.expr).bad()) {
                l.count();
                bool runtime = std::any_of(c.items.begin(), c.items.end(), [&](const auto& g) {
                    return std::any_of(g.expressions.begin(), g.expressions.end(),
                                       [&](const Expression* e) { return runtimeItem(l, *e); });
                });
                if (runtime) {
                    checkMixedCase(c);
                }
                else {
                    ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
                    EvalContext ec(ac);
                    auto [taken, known] = c.getKnownBranch(ec);
                    if (!known) {
                        l.report(c.sourceRange.start(),
                                 "constant case expression: its items could not be evaluated: which arms are reachable is not decided",
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
        : LintRule("LNT-038") {}

private:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::string hier;
        lint::ValueRanges ranges;
        explicit V(Lint& lint) : l(lint), ranges(lint) {}

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

        void report(slang::SourceLocation at, std::string msg, const Expression* konst, std::string identity = {}) {
            Severity sev = konst && referencesSignal(*konst) ? Severity::Info : Severity::Warning;
            l.report(at, std::move(msg),
                     {.severity = sev,
                      .nextStep = "A comparison that never changes is usually a typo or a width mistake; fix the operand or the constant.",
                      .hierarchy = hier, .identityKey = std::move(identity)});
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
            bool ranged = false;
            if (pureRef(lhs) && isRuntime(l, lhs) && !isRuntime(l, rhs)) {
                operand = &lhs;
                konst = &rhs;
            }
            else if (pureRef(rhs) && isRuntime(l, rhs) && !isRuntime(l, lhs)) {
                operand = &rhs;
                konst = &lhs;
                op = mirror(op);
            }
            else if (isRuntime(l, lhs) && !isRuntime(l, rhs)) {
                operand = &lhs;
                konst = &rhs;
                ranged = true;
            }
            else if (isRuntime(l, rhs) && !isRuntime(l, lhs)) {
                operand = &rhs;
                konst = &lhs;
                op = mirror(op);
                ranged = true;
            }
            else {
                return;
            }
            auto cv = l.evalConst(*konst);
            if (!cv.isInteger() || cv.integer().hasUnknown()) return;   // real or X/Z constants: other rules
            const Type& ot = *operand->type;
            if (!ot.isIntegral()) return;
            if (ranged) {
                rangedCompare(b, *operand, *konst, cv.integer(), op);
                return;
            }
            int64_t w = ot.getBitWidth();
            std::vector<std::pair<int64_t, int64_t>> ivs;
            auto c = smallValue(cv.integer());
            bool cSigned = konst->type->isSigned();
            // A negative constant no wider than an unsigned operand is a bit pattern there (zero-extended into the unsigned
            // comparison), one the operand can take: compare by that pattern.
            const int64_t cw = konst->type->getBitWidth();
            if (c && *c < 0 && !ot.isSigned() && cSigned && cw <= w && cw <= 62) {
                c = *c + (int64_t(1) << cw);
                cSigned = false;
            }
            // A wider unsigned operand takes every value from 0 up past 2^62, so against zero, a fill literal or a constant that
            // fits in 62 bits only `< 0` / `>= 0` (and `> '1` / `<= '1`) are constant.
            if (w > 62 && !ot.isSigned()) {
                const bool allOnes = konst->kind == ExpressionKind::UnbasedUnsizedIntegerLiteral && cv.integer().getBitWidth() >= w &&
                                     cv.integer().countOnes() == cv.integer().getBitWidth();
                const bool small = c && *c >= 0 && *c < (int64_t(1) << 62);
                if (small || allOnes) {
                    const bool zero = small && *c == 0;
                    std::optional<bool> result;
                    if (zero && op == BinaryOperator::LessThan) result = false;
                    else if (zero && op == BinaryOperator::GreaterThanEqual) result = true;
                    else if (allOnes && op == BinaryOperator::GreaterThan) result = false;
                    else if (allOnes && op == BinaryOperator::LessThanEqual) result = true;
                    if (result)
                        report(b.sourceRange.start(),
                               std::string("comparison of a ") + std::to_string(w) + "-bit unsigned operand with " +
                                   (zero ? "0" : "all ones") + " is always " + (*result ? "true" : "false") + " for known values",
                               konst, std::to_string(w) + ":0:" + (zero ? "0" : "ones"));
                    return;
                }
            }
            if (!c || w == 0 || w > 62 || !operandDomain(ot.isSigned(), w, cSigned, *c, ivs)) {
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
                   konst, std::to_string(w) + ":" + std::to_string(ot.isSigned()) + ":" + std::to_string(*c));
        }

        // An operand that is not a plain reference (a concatenation with a constant zero top, a mask, a sum in a wide context) is judged by
        // its value range (ANALYSIS.md, Value ranges): the comparison is constant when every value in the range gives one answer. Only a
        // non-negative range against a non-negative constant, so signedness cannot change the comparison.
        void rangedCompare(const BinaryExpression& b, const Expression& operand, const Expression& konst, const SVInt& cv, BinaryOperator op) {
            const auto c = smallValue(cv);
            const auto range = ranges.of(operand);
            if (!c || *c < 0 || !range || range->lo < 0 || range->hi > (Wide(1) << 62)) return;
            // not narrower than the type: nothing the type-based test of a plain reference would not already say, and it is not modelled here
            const auto full = lint::typeRange(*operand.type);
            if (!full || (range->lo == full->lo && range->hi == full->hi)) return;
            std::vector<std::pair<int64_t, int64_t>> ivs;
            ivs.emplace_back(static_cast<int64_t>(range->lo), static_cast<int64_t>(range->hi));
            const auto result = constantResult(op, *c, ivs);
            if (!result) return;
            report(b.sourceRange.start(),
                   "comparison of a value that ranges over " + lint::toString(*range) + " with constant " + std::to_string(*c) + " is always " +
                       (*result ? "true" : "false") + " for known values",
                   &konst, "range:" + lint::toString(*range) + ":" + std::to_string(*c) + ":" + std::to_string(static_cast<int>(op)));
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
// LNT-045: legacy always @(a or b ...) with an explicit level list: a module-level signal read by the body (before
// the body assigns it) but missing from the list is a sensitivity mismatch (WARNING); a listed signal the body never
// reads is INFO. always @*, always_comb and edge-sensitive blocks are not checked.

// What the visible user functions a block calls read of the module's own state. `certain` are the signals every call reads (a read in
// straight-line code of the callee); `possible` those read only under a condition, a loop or a short-circuit. `complete` is false when a
// callee cannot be read (a task, a function with an output, inout or ref argument, DPI or a virtual method) or writes module state.
struct CalleeReads {
    std::set<const Symbol*> certain, possible;
    bool complete = true;
    bool called = false;   // the block calls a visible user function at all (else its opacity has another cause: a timing control)
};

CalleeReads calleeReadsOf(const Statement& body) {
    struct Reads : ASTVisitor<Reads, VisitFlags::AllGood> {
        const SubroutineSymbol* sub = nullptr;
        CalleeReads& out;
        int depth = 0;
        std::set<const SubroutineSymbol*> seen;
        std::vector<const SubroutineSymbol*> pending;
        explicit Reads(CalleeReads& o) : out(o) {}
        bool local(const Symbol& sym) const {
            for (const Scope* sc = sym.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
                if (&sc->asSymbol() == sub) return true;
            return false;
        }
        void handle(const NamedValueExpression& e) {
            const Symbol& sym = e.symbol;
            if (!sub || (sym.kind != SymbolKind::Variable && sym.kind != SymbolKind::Net) || local(sym)) return;
            (depth == 0 ? out.certain : out.possible).insert(&sym);
        }
        void handle(const HierarchicalValueExpression&) { out.complete = false; }
        void handle(const AssignmentExpression& a) {
            if (sub) {
                // a write to module state from a function is a side effect this walk does not follow
                const Symbol* target = a.left().getSymbolReference();
                if (!target || (!local(*target) && target->kind != SymbolKind::FormalArgument)) out.complete = false;
            }
            visitDefault(a);
        }
        void handle(const CallExpression& c) {
            if (c.isSystemCall()) { visitDefault(c); return; }
            const auto* callee = std::get_if<const SubroutineSymbol*>(&c.subroutine);
            if (!callee || !*callee || (*callee)->subroutineKind != SubroutineKind::Function || c.thisClass()) { out.complete = false; return; }
            for (const auto* formal : (*callee)->getArguments())
                if (formal->direction != ArgumentDirection::In) out.complete = false;
            out.called = true;
            if (seen.insert(*callee).second) pending.push_back(*callee);
            visitDefault(c);   // the actual arguments are read by the block itself (Flow sees those)
        }
        void handle(const ConditionalStatement& s) { for (auto& c : s.conditions) c.expr->visit(*this); ++depth; s.ifTrue.visit(*this); if (s.ifFalse) s.ifFalse->visit(*this); --depth; }
        void handle(const CaseStatement& s) {
            s.expr.visit(*this);
            ++depth;
            for (const auto& item : s.items) { for (auto* e : item.expressions) e->visit(*this); item.stmt->visit(*this); }
            if (s.defaultCase) s.defaultCase->visit(*this);
            --depth;
        }
        void handle(const ConditionalExpression& e) { for (auto& c : e.conditions) c.expr->visit(*this); ++depth; e.left().visit(*this); e.right().visit(*this); --depth; }
        void handle(const BinaryExpression& e) {
            const bool shortCircuit = e.op == BinaryOperator::LogicalAnd || e.op == BinaryOperator::LogicalOr || e.op == BinaryOperator::LogicalImplication;
            if (!shortCircuit) { visitDefault(e); return; }
            e.left().visit(*this);
            ++depth; e.right().visit(*this); --depth;
        }
        void handle(const ForLoopStatement& s) { ++depth; visitDefault(s); --depth; }
        void handle(const WhileLoopStatement& s) { ++depth; visitDefault(s); --depth; }
        void handle(const DoWhileLoopStatement& s) { ++depth; visitDefault(s); --depth; }
        void handle(const RepeatLoopStatement& s) { ++depth; visitDefault(s); --depth; }
        void handle(const ForeachLoopStatement& s) { ++depth; visitDefault(s); --depth; }
        void handle(const ForeverLoopStatement& s) { ++depth; visitDefault(s); --depth; }
    };
    CalleeReads out;
    Reads r(out);
    body.visit(r);   // the block itself: only its calls matter (sub is null)
    while (!r.pending.empty()) {
        r.sub = r.pending.back();
        r.pending.pop_back();
        r.depth = 0;
        r.sub->getBody().visit(r);
    }
    return out;
}

void checkSensitivity(Lint& l, const ProceduralBlockSymbol& p) {
    const TimingControl& t = p.getBody().as<TimedStatement>().timing;
    std::vector<const TimingControl*> events;
    if (t.kind == TimingControlKind::EventList)
        events.assign(t.as<EventListControl>().events.begin(), t.as<EventListControl>().events.end());
    else
        events.push_back(&t);

    // Listed entries as (symbol, bit range): a static select such as a[0] is sensitive to those bits only (IEEE 1800 9.4.2).
    slang::ast::EvalContext ec(p);
    std::set<const Symbol*> listed, uncertainListed;
    std::map<const Symbol*, std::vector<fl::Range>> listedRanges;
    bool unresolved = false;
    for (auto* e : events) {
        auto& expr = e->as<SignalEventControl>().expr;
        ValuePath vp(expr, ec);
        auto* sym = vp.rootSymbol();
        if (!sym || !sym->isValue()) { unresolved = true; continue; }
        listed.insert(sym);
        // A runtime select can observe only the currently selected bits. It cannot
        // establish coverage of the root; leave its coverage unresolved.
        if (!vp.isFullyStatic()) { unresolved = true; uncertainListed.insert(sym); continue; }
        uint64_t w = sym->as<ValueSymbol>().getType().isFixedSize() ? sym->as<ValueSymbol>().getType().getBitstreamWidth() : 1;
        fl::Range r{0, w ? w - 1 : 0};
        if (vp.lsp && vp.isFullyStatic()) { r = vp.lspBounds; if (r.first > r.second) std::swap(r.first, r.second); }
        listedRanges[sym].push_back(r);
    }

    fl::Flow flow(p);
    std::set<const Symbol*> external, anyRead;
    for (auto& a : flow.accesses) {
        if (a.write || a.local) continue;
        anyRead.insert(a.sym);
        if (!a.entryMaybe) continue;   // every path reads a value this activation wrote itself
        // some path reads the value from before this activation: every bit read must be covered by a listed range
        bool covered = false;
        if (auto it = listedRanges.find(a.sym); it != listedRanges.end()) {
            std::vector<bool> bits(a.range.second - a.range.first + 1, false);
            for (auto& lr : it->second)
                for (uint64_t b = std::max(lr.first, a.range.first); b <= std::min(lr.second, a.range.second) && lr.first <= lr.second; b++) bits[b - a.range.first] = true;
            covered = std::all_of(bits.begin(), bits.end(), [](bool x) { return x; });
        }
        if (!covered) external.insert(a.sym);
    }

    auto minus = [](const std::set<const Symbol*>& a, const std::set<const Symbol*>& b) {
        std::vector<std::string> out;
        for (auto* s : a)
            if (!b.count(s)) out.push_back(std::string(s->name));
        return out;
    };
    auto unused = minus(listed, anyRead);
    std::string where = p.getHierarchicalPath();

    // An entry listed through a runtime select makes only the symbols it names uncertain; a plainly missing signal is certain.
    std::vector<std::string> certainMissing, uncertainMissing;
    for (auto* s : external) (uncertainListed.count(s) ? uncertainMissing : certainMissing).push_back(std::string(s->name));
    // A visible function the block calls reads module state too: a signal it always reads and the list lacks is a certain miss.
    // What it reads only under a condition keeps the block's result Unresolved, but is named.
    bool calleesOpen = flow.opaque;
    std::vector<std::string> possibleMissing;
    if (flow.opaque) {
        const CalleeReads callee = calleeReadsOf(p.getBody());
        calleesOpen = !callee.complete || !callee.called;
        // A signal the block itself writes in every activation before EACH call that can read it is read as the block left it: it is no input
        // of the block, and listing it would make the block trigger itself. A call that runs before the write (or on a path without it)
        // reads the value from before the activation, so it counts.
        struct UserCalls : ASTVisitor<UserCalls, VisitFlags::AllGood> {
            std::vector<const CallExpression*> calls;
            void handle(const CallExpression& c) { if (!c.isSystemCall()) calls.push_back(&c); visitDefault(c); }
        } userCalls;
        p.getBody().visit(userCalls);
        const auto writtenFirst = [&](const Symbol* sym) {
            const auto* value = sym->as_if<ValueSymbol>();
            if (!value || userCalls.calls.empty()) return false;
            for (const auto* call : userCalls.calls)
                if (!flow.entryRangesAtCall(*call, *value).empty()) return false;
            return true;
        };
        for (auto* sym : callee.certain)
            if (!listed.count(sym) && !external.count(sym) && !writtenFirst(sym)) certainMissing.push_back(std::string(sym->name));
        for (auto* sym : callee.possible)
            if (!listed.count(sym) && !external.count(sym) && !callee.certain.count(sym) && !writtenFirst(sym)) { possibleMissing.push_back(std::string(sym->name)); calleesOpen = true; }
    }
    if (!certainMissing.empty())
        l.report(p.location,
                 "always @(...) reads '" + joinNames(certainMissing) + "' but they are not in its sensitivity list: the block does not re-run when they change",
                 {.confidence = Confidence::Established,
                  .nextStep = "Add the signals to the list, or use always @* / always_comb.",
                  .hierarchy = where,
                  .signals = certainMissing});
    if (!uncertainMissing.empty())
        l.report(p.location,
                 "always @(...) reads '" + joinNames(uncertainMissing) + "' but its sensitivity list names them only through a runtime select: whether every bit read is covered is not established",
                 {.confidence = Confidence::Unresolved,
                  .nextStep = "List the signal itself, or use always @* / always_comb.",
                  .hierarchy = where,
                  .signals = uncertainMissing});
    if (!unused.empty() && !flow.opaque)
        l.report(p.location,
                 "sensitivity list entries '" + joinNames(unused) + "' are never read by the block",
                 {.severity = Severity::Info,
                  .nextStep = "Remove unused entries from the list.",
                  .hierarchy = where,
                  .signals = unused});
    if (unresolved) {
        l.context().degradedReason = "sensitivity expression bit coverage is not established";
        l.report(p.location, "sensitivity list entry has an unproved expression or dynamic select: its bit coverage is not established",
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = where});
    }
    if (calleesOpen)
        l.report(p.location,
                 std::string("block calls a subroutine: signals read inside it are not all checked against the sensitivity list") +
                     (possibleMissing.empty() ? "" : " (it may read '" + joinNames(possibleMissing) + "', which the list lacks, on some paths)"),
                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved, .hierarchy = where, .signals = possibleMissing});
}

class SensitivityRule final : public LintRule {
public:
    SensitivityRule() : LintRule("LNT-045") {}

private:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location) && p.procedureKind == ProceduralBlockKind::Always && fl::styleOf(p) == fl::Style::Level)
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
    v.push_back(std::make_unique<SensitivityRule>());
    return v;
}

} // namespace sverity
