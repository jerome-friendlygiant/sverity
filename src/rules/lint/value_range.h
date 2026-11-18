// One value-range proof for the width and sign rules (LNT-004, 005, 034, 036): an interval of the values an integral expression
// can take, built from constants (exactly), `for` loop variables (the loop facts of loop_facts.h), the width and signedness of
// every other operand, and the arithmetic, shifts, bitwise masks, conditionals, concatenations and conversions above them.
// A rule drops or demotes a finding only when this proof shows the value is preserved at that site; anything it cannot model is
// the operand's full range, never a narrower guess. See docs/rules/ANALYSIS.md (Value ranges).
#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rules/lint/loop_facts.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/types/Type.h"

namespace sverity::lint {

using Wide = __int128;

/// A closed interval of integers, wide enough for every 64-bit type and for a product of two 32-bit values.
struct Interval {
    Wide lo = 0, hi = 0;
    bool within(const Interval& o) const { return lo >= o.lo && hi <= o.hi; }
    bool nonNegative() const { return lo >= 0; }
    bool operator==(const Interval& o) const { return lo == o.lo && hi == o.hi; }
};

std::string toString(Wide v);
std::string toString(const Interval& i);   // "5" for a point, "[lo, hi]" otherwise

/// Every value of an integral type of at most 64 bits; nothing for any other type.
std::optional<Interval> typeRange(const slang::ast::Type& t);

/// What a statement (and the subroutines it calls, transitively) may write by a blocking assignment, an increment or an output,
/// inout or ref argument. A nonblocking write is not listed: the reads of the same activation still see the old value.
/// `unknown` when a callee cannot be read (DPI, extern, virtual, a system call that stores into its arguments).
struct BlockingWrites {
    std::unordered_set<const slang::ast::Symbol*> symbols;
    bool unknown = false;
};
BlockingWrites blockingWritesIn(const slang::ast::Statement& body);

class ValueRanges {
public:
    /// `allowWrap`: a counter that wraps in its own type still runs its leading iterations; `enter` then gives those (see `wraps`).
    /// `formals`: an `input` formal of a function that every call passes constants (or values of a known range) to has the range of
    /// what the calls pass (see formalRangesOf in value_range.cpp); false gives it its type's range.
    explicit ValueRanges(Lint& l, bool allowWrap = false, bool formals = true) : l_(l), allowWrap_(allowWrap), formals_(formals) {}

    /// A `for` loop whose variable has a known range (constantLoopValues) puts it in scope until `leave`. Inside the loop's stop test
    /// the variable also takes the value that ends the loop; in its steps and body only the values the body sees.
    /// Returns false when the loop has no such range (its variable then has its type's range).
    bool enter(const slang::ast::ForLoopStatement& loop);
    void leave(const slang::ast::ForLoopStatement& loop);
    void testPhase(const slang::ast::ForLoopStatement& loop, bool on);

    /// Visits the parts of a `for` loop with the right ranges in scope: initializers outside, the stop test with the ending value,
    /// the steps and the body with the body's values.
    template <typename V>
    void visitLoop(V& visitor, const slang::ast::ForLoopStatement& s) {
        for (auto* v : s.loopVars)
            if (auto* init = v->getInitializer()) init->visit(visitor);
        for (auto* e : s.initializers) e->visit(visitor);
        const bool known = enter(s);
        if (known) testPhase(s, true);
        if (s.stopExpr) s.stopExpr->visit(visitor);
        if (known) testPhase(s, false);
        for (auto* e : s.steps) e->visit(visitor);
        s.body.visit(visitor);
        if (known) leave(s);
    }

    /// True when `var` is a `for` loop variable with a known range in scope, and the values its body sees (nothing otherwise).
    bool isLoopVar(const slang::ast::ValueSymbol* var) const { return loops_.count(var) != 0; }
    std::optional<Interval> loopRange(const slang::ast::ValueSymbol* var) const;

    /// Inside a branch that a condition decides, the variable takes only the values of `allowed` (intersected with its range).
    /// `empty` is set when none is left, so the branch never runs. `restore` with the value returned ends it.
    std::optional<Interval> narrow(const slang::ast::ValueSymbol* var, const Interval& allowed, bool& empty);
    void restore(const slang::ast::ValueSymbol* var, const std::optional<Interval>& saved);

    /// A condition that decides a branch also decides variables: `guardsOf(cond, truth)` lists the variables the condition constrains
    /// when it evaluates to `truth`, each as an over-approximating interval of its values: a comparison of a variable with a constant
    /// (through `!`, `&&` and `||`), and a net or variable whose one driver is a continuous assignment of such a condition (`assign done
    /// = q >= N`). Narrowing puts them in scope for a branch (`Narrowing`, scoped); `of` then gives the variable the narrower interval.
    /// The caller guarantees that the branch does not write the variable before it reads it (the guard names a value, not a place).
    struct Guard {
        const slang::ast::ValueSymbol* var;
        Interval range;
    };
    std::vector<Guard> guardsOf(const slang::ast::Expression& cond, bool truth) const;
    class Narrowing {
    public:
        Narrowing(ValueRanges& ranges, const std::vector<Guard>& guards);
        ~Narrowing();
        Narrowing(const Narrowing&) = delete;
        Narrowing& operator=(const Narrowing&) = delete;

    private:
        ValueRanges& r_;
        std::vector<std::pair<const slang::ast::ValueSymbol*, std::optional<Interval>>> saved_;
    };

    /// True when `e` names a loop variable whose range covers only the leading iterations of a wrapping counter: it proves an
    /// overrun, never that a value stays in range.
    bool wraps(const slang::ast::Expression& e) const;

    /// True when the value of `e` is fixed by constants and `for` loop variables with a known range alone (arithmetic, shifts,
    /// conversions, concatenations and conditionals of those): no design input, signal or call decides it. Its `of` range is then
    /// what the code itself produces; for any other expression the range is the most any input could make it.
    bool determined(const slang::ast::Expression& e) const;

    /// The values the expression can take, as the expression is typed in its context; nothing when it is not integral or wider
    /// than 64 bits. Never narrower than the truth: an operation that may wrap gives its type's whole range. Unknown (X/Z) bits
    /// are not modelled.
    std::optional<Interval> of(const slang::ast::Expression& e) const;

    /// The mathematical value range of an arithmetic operation (+ - * << >> & | ^ / % of operands as typed), before the result
    /// is reduced to its type's width. Nothing for any other expression or an operand the model does not cover.
    std::optional<Interval> mathematical(const slang::ast::Expression& e) const;

    /// True when `e` (+ - * << ...) keeps its mathematical value in its own evaluated width: no bit is lost to wrap-around.
    bool wrapFree(const slang::ast::Expression& e) const;

    /// True when every implicit conversion layer around `e` keeps the value: the interval of what goes in lies inside the type
    /// that comes out. `e` itself without conversions is trivially preserved.
    bool preservedThrough(const slang::ast::Expression& e) const;

    /// The expression as the author wrote it: text for messages, or an empty string when it spans a macro.
    std::string text(const slang::ast::Expression& e) const;

    Lint& lint() const { return l_; }

private:
    struct LoopRange {
        Interval body, test;
        bool inTest = false;
    };
    std::vector<Guard> guardsOf(const slang::ast::Expression& cond, bool truth, int depth) const;
    std::optional<Interval> binary(const slang::ast::BinaryExpression& b, bool clamp) const;
    std::optional<std::pair<const slang::ast::ValueSymbol*, Interval>> loopComparison(const slang::ast::Expression& cond, bool truth) const;
    std::optional<Interval> assignedOnceRange(const slang::ast::VariableSymbol& var) const;
    bool tableElements(const slang::ast::Expression& e, std::vector<const slang::ConstantValue*>& out) const;
    std::optional<Interval> valueIn(const slang::ast::Expression& operand, const slang::ast::Type& to) const;
    Lint& l_;
    bool allowWrap_ = false;
    bool formals_ = true;
    std::unordered_set<const slang::ast::ValueSymbol*> wrapping_;
    std::unordered_map<const slang::ast::ValueSymbol*, LoopRange> loops_;
    std::unordered_map<const slang::ast::ValueSymbol*, Interval> guards_;
    std::unordered_map<const slang::ast::ForLoopStatement*, const slang::ast::ValueSymbol*> entered_;
};

} // namespace sverity::lint
