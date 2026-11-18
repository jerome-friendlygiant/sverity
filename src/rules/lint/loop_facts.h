// Facts about `for` loops and their variables that more than one lint rule relies on: the values a loop variable takes,
// whether a body writes it, whether a body may leave early, and which constructs guard a node. One definition, used by
// the LNT-005 sign proofs, the LNT-006 / LNT-037 value ranges and the LNT-008 / LNT-024 driven-bit proofs.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/text/SourceManager.h"
#include "sverity/lint.h"

namespace sverity::lint {

/// The expression with its implicit conversions removed: the operand as the author wrote it.
const slang::ast::Expression& unwrapImplicit(const slang::ast::Expression& e);

/// The value of an integer constant expression, or nothing when it is not one (or has unknown bits).
std::optional<int64_t> constantInt(const Lint& l, const slang::ast::Expression& e);

/// The constant step a `for` loop applies to `var`: +c for `var++`, `var += c` and `var = var + c`, -c for the decrements.
/// Nothing for any other step, a step that is not constant, or a step by zero or less.
std::optional<int64_t> loopStep(const slang::ast::ForLoopStatement& loop, const slang::ast::ValueSymbol* var, Lint& l);

/// True when a statement may write `var`: an assignment or step to it (an index inside an assignment target is only read),
/// or a call that passes it as an output, inout or ref argument.
/// A variable declared by the loop itself cannot be named by a subroutine, so a call writes it only through an argument.
bool writesVariable(const slang::ast::Statement& body, const slang::ast::ValueSymbol* var, bool declaredByLoop = false);

/// The values a `for` loop variable takes: first, first + step, ..., count values in all (count >= 1).
struct LoopValues {
    const slang::ast::ValueSymbol* var = nullptr;
    int64_t first = 0, step = 0;
    uint64_t count = 0;
    bool wraps = false;   // the counter later leaves its type; only the leading `count` iterations are known
    int64_t at(uint64_t k) const { return first + static_cast<int64_t>(k) * step; }
    int64_t min() const { return step > 0 ? first : at(count - 1); }
    int64_t max() const { return step > 0 ? at(count - 1) : first; }
};

/// The values of the loop variable, when the loop starts at a constant, has a constant bound, steps by a constant towards
/// that bound, and its body never writes the variable. Nothing otherwise, including a loop that runs zero times.
/// A counter that wraps in its own type gives nothing, unless `allowWrap`: then the iterations before the wrap (`wraps` set).
std::optional<LoopValues> constantLoopValues(const slang::ast::ForLoopStatement& loop, Lint& l, bool allowWrap = false);

/// True when the expression calls a user subroutine. Its value evaluated here says nothing of the hardware: the callee may keep state
/// between activations (a static local), so a condition that calls one is never taken as constant.
bool callsSubroutine(const slang::ast::Expression& e);

/// True when the first test of a `for` loop is false for the initial values (it calls no user subroutine and every variable it reads is a loop counter with a
/// constant start): the body never runs, whatever else the loop does.
bool neverRuns(const slang::ast::ForLoopStatement& loop, Lint& l);

/// Whether a statement (a loop body) may leave the loop around it early, and whether an exit may stop later iterations from
/// running (`notRuntime`). Each exit's conditions are evaluated for every iteration with the loop variable known (`values`) and
/// every read of design state unknown: an exit that certainly runs in some iteration stops the later ones; one that only may run,
/// depending on design state, leaves their writes in the hardware (not `notRuntime`); one that cannot be evaluated (an enclosing
/// loop's counter, a counter with no known range, an unreadable callee, an unmodelled construct) is `notRuntime` too, as the
/// finding must then be Unresolved. `otherVars` are counters of enclosing loops; `scope` is the procedure or subroutine around the
/// loop, to read the locals declared outside it. break and continue count only when they leave this loop (not in a nested loop or
/// a callee); a disable of a block inside the statement does not; a call counts when its callee leaves an outer scope or cannot be
/// read (DPI, extern, virtual).
struct EarlyExitInfo { bool leaves = false, notRuntime = false, continues = false; };   // continues: a `continue` certainly (or possibly, unevaluably) runs in some iteration
EarlyExitInfo earlyExit(const Lint& l, const slang::ast::Statement& body, const std::optional<LoopValues>& values,
                        const std::vector<const slang::ast::ValueSymbol*>& otherVars = {}, const slang::ast::Statement* scope = nullptr);
bool leavesEarly(const slang::ast::Statement& body, bool wholeProcedure = false);

/// True when the statement certainly ends the block it sits in: an unconditional `break`, `continue`, `return` or a `disable` of one of the
/// `enclosing` blocks, alone or last in nested sequential blocks. What follows it in the same block never runs and builds no hardware.
bool alwaysJumps(const slang::ast::Statement& s, const std::vector<const slang::ast::Symbol*>& enclosing);

/// Which iterations of a loop run, when every exit of its body can be judged: a `break` or `continue` of the loop whose conditions are
/// evaluated for every iteration (enclosing loops' counters in `outers` take every combination of their values; design state is unknown).
/// `stops` holds, for each combination, the iterations at which an exit certainly runs, with its position. Nothing when an exit cannot
/// be judged (a call, a return, a disable, an unevaluable condition) or the loop is too long to enumerate.
struct ExitStop { uint64_t iteration; bool isContinue; slang::SourceLocation loc; };
struct ExitProfile {
    LoopValues values;
    std::vector<LoopValues> outers;
    std::vector<std::vector<int64_t>> combos;      // the values of `outers` in each combination
    std::vector<std::vector<ExitStop>> stops;      // per combination
    /// The iterations of combination `c` that can run for a statement at `at`: those up to the first certain `break` (that iteration
    /// included unless the break precedes the statement), without those a certain `continue` before `at` skips.
    /// Nothing when the order of an exit and the statement cannot be established (both inside one macro expansion).
    const slang::SourceManager* sm = nullptr;
    std::optional<std::vector<uint64_t>> live(size_t c, slang::SourceLocation at) const {
        // `end` is one past the last live iteration: a certain break stops its own iteration for a statement after it
        uint64_t end = values.count ? values.count : 1;
        for (const auto& s : stops[c]) {
            auto before = precedes(s.loc, at);
            if (!before) return std::nullopt;
            if (!s.isContinue) end = std::min(end, s.iteration + (*before ? 0 : 1));
        }
        std::vector<uint64_t> out;
        for (uint64_t k = 0; k < end; k++) {
            bool skipped = false;
            for (const auto& s : stops[c]) {
                if (!s.isContinue || s.iteration != k) continue;
                skipped = skipped || *precedes(s.loc, at);
            }
            if (!skipped) out.push_back(k);
        }
        return out;
    }
    /// Whether `a` comes before `b` in the source; a place inside a macro expansion counts where the outermost macro was invoked
    /// (never where its text was defined), and two places expanded at one invocation have no established order.
    std::optional<bool> precedes(slang::SourceLocation a, slang::SourceLocation b) const {
        if (!sm || (!sm->isMacroLoc(a) && !sm->isMacroLoc(b))) return a < b;
        const auto fa = sm->getFullyExpandedLoc(a), fb = sm->getFullyExpandedLoc(b);
        if (fa == fb) return std::nullopt;
        return fa < fb;
    }
};
std::optional<ExitProfile> exitProfile(const Lint& l, const slang::ast::Statement& body, const std::optional<LoopValues>& values,
                                      const std::vector<LoopValues>& outers, const std::vector<const slang::ast::ValueSymbol*>& otherVars,
                                      const slang::ast::Statement* scope);

/// Counts, in `guarded`, the constructs around the node being visited that may not run it: conditional branches,
/// short-circuit right operands and every loop except `for` (a derived visitor decides `for` loops itself). Conditions,
/// case selectors and short-circuit left operands are visited before entering the guarded region. A node visited at zero
/// depth runs whenever the procedure around it does, so a derived visitor may rely on that.
template<typename Derived>
class ConditionalDepthVisitor : public HardwareVisitor<Derived, slang::ast::VisitFlags::AllCanonical> {
public:
    void handle(const slang::ast::ConditionalStatement& s) {
        bool conditional = false;
        for (const auto& condition : s.conditions) {
            if (conditional) ++guarded;
            condition.expr->visit(*static_cast<Derived*>(this));
            if (condition.pattern) condition.pattern->visit(*static_cast<Derived*>(this));
            if (conditional) --guarded;
            conditional = true;
        }
        ++guarded;
        s.visitStmts(*static_cast<Derived*>(this));
        --guarded;
    }
    void handle(const slang::ast::CaseStatement& s) {
        s.expr.visit(*static_cast<Derived*>(this));
        ++guarded;
        for (const auto& item : s.items) {
            for (auto expr : item.expressions) expr->visit(*static_cast<Derived*>(this));
            item.stmt->visit(*static_cast<Derived*>(this));
        }
        if (s.defaultCase) s.defaultCase->visit(*static_cast<Derived*>(this));
        --guarded;
    }
    void handle(const slang::ast::PatternCaseStatement& s) {
        s.expr.visit(*static_cast<Derived*>(this));
        ++guarded;
        for (const auto& item : s.items) {
            item.pattern->visit(*static_cast<Derived*>(this));
            if (item.filter) item.filter->visit(*static_cast<Derived*>(this));
            item.stmt->visit(*static_cast<Derived*>(this));
        }
        if (s.defaultCase) s.defaultCase->visit(*static_cast<Derived*>(this));
        --guarded;
    }
    void handle(const slang::ast::ConditionalExpression& e) {
        bool conditional = false;
        for (const auto& condition : e.conditions) {
            if (conditional) ++guarded;
            condition.expr->visit(*static_cast<Derived*>(this));
            if (condition.pattern) condition.pattern->visit(*static_cast<Derived*>(this));
            if (conditional) --guarded;
            conditional = true;
        }
        ++guarded;
        e.left().visit(*static_cast<Derived*>(this));
        e.right().visit(*static_cast<Derived*>(this));
        --guarded;
    }
    void handle(const slang::ast::WhileLoopStatement& s) { guard(s); }
    void handle(const slang::ast::DoWhileLoopStatement& s) { guard(s); }
    void handle(const slang::ast::RepeatLoopStatement& s) { guard(s); }
    void handle(const slang::ast::ForeachLoopStatement& s) { guard(s); }
    void handle(const slang::ast::ForeverLoopStatement& s) { guard(s); }
    void handle(const slang::ast::BinaryExpression& e) {
        const bool shortCircuit = e.op == slang::ast::BinaryOperator::LogicalAnd || e.op == slang::ast::BinaryOperator::LogicalOr ||
                                  e.op == slang::ast::BinaryOperator::LogicalImplication;
        if (shortCircuit) {
            e.left().visit(*static_cast<Derived*>(this));
            ++guarded;
            e.right().visit(*static_cast<Derived*>(this));
            --guarded;
        }
        else this->visitDefault(e);
    }

protected:
    int guarded = 0;
    template<typename T> void guard(const T& node) {
        ++guarded;
        this->visitDefault(node);
        --guarded;
    }
};

} // namespace sverity::lint
