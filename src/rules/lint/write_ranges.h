#pragma once
#include "domains/model.h"
#include "rules/lint/loop_facts.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/types/AllTypes.h"

namespace sverity::lint {
using WriteRanges = std::vector<std::pair<uint64_t, uint64_t>>;
inline std::optional<WriteRanges> selectedRanges(const slang::ast::Expression& expr,
                                                const slang::ast::ValueSymbol& root,
                                                slang::ast::EvalContext& ec) {
    WriteRanges ranges;
    bool exact = true;
    slang::ast::ValuePath::visitPaths(expr, ec, [&](const slang::ast::ValuePath& path) {
        if (path.rootSymbol() != &root) return;
        if (!path.isFullyStatic()) { exact = false; return; }
        auto [lo, hi] = path.lspBounds;
        ranges.emplace_back(std::min(lo, hi), std::max(lo, hi));
    }, true);
    if (!exact || ranges.empty()) return std::nullopt;
    return ranges;
}
/// The bits of `root` an expression selects through its longest static prefix, ordered low to high. A dynamic select widens
/// to the prefix (a superset). Nothing when the expression does not reach `root`.
inline std::optional<std::pair<uint64_t, uint64_t>> prefixBits(const slang::ast::Expression& expr,
                                                              const slang::ast::ValueSymbol& root,
                                                              slang::ast::EvalContext& ec) {
    std::optional<std::pair<uint64_t, uint64_t>> result;
    slang::ast::ValuePath::visitPaths(expr, ec, [&](const slang::ast::ValuePath& path) {
        if (path.rootSymbol() != &root) return;
        auto [lo, hi] = path.lsp ? path.lspBounds : std::pair<uint64_t, uint64_t>{0, UINT64_MAX};
        if (lo > hi) std::swap(lo, hi);
        if (!result) result = std::pair{lo, hi};
        else result = std::pair{std::min(result->first, lo), std::max(result->second, hi)};
    }, true);
    return result;
}
inline std::optional<WriteRanges> writeRanges(const model::ProceduralWrite& write, Lint& l) {
    if (!write.target || write.called) return std::nullopt;
    if (write.exactTarget) return WriteRanges{{write.lo, write.hi}};
    if (write.fromCall || !write.targetExpr) return std::nullopt;
    std::vector<LoopValues> loops;
    uint64_t count = 1;
    for (auto* scope : write.scopes) {
        if (scope->kind == slang::ast::StatementKind::WhileLoop || scope->kind == slang::ast::StatementKind::DoWhileLoop ||
            scope->kind == slang::ast::StatementKind::RepeatLoop) return std::nullopt;
        if (scope->kind == slang::ast::StatementKind::ForeachLoop) {
            const auto& loop = scope->as<slang::ast::ForeachLoopStatement>();
            if (leavesEarly(loop.body)) return std::nullopt;
            for (const auto& dim : loop.loopDims) {
                if (!dim.loopVar) continue;
                if (!dim.range || dim.range->width() > 4096 / count) return std::nullopt;
                count *= dim.range->width();
                loops.push_back({dim.loopVar, dim.range->lower(), 1, dim.range->width(), false});
            }
            continue;
        }
        if (scope->kind != slang::ast::StatementKind::ForLoop) continue;
        const auto& loop = scope->as<slang::ast::ForLoopStatement>();
        auto values = constantLoopValues(loop, l);
        if (!values || leavesEarly(loop.body) || values->count > 4096 / count) return std::nullopt;
        count *= values->count;
        loops.push_back(*values);
    }
    slang::ast::EvalContext ec(*write.process);
    ec.pushEmptyFrame();
    std::vector<slang::ConstantValue*> locals;
    for (auto& loop : loops) locals.push_back(ec.createLocal(loop.var));
    WriteRanges result;
    for (uint64_t k = 0; k < count; ++k) {
        auto remainder = k;
        for (size_t i = 0; i < loops.size(); ++i) {
            const auto& type = loops[i].var->getType();
            *locals[i] = slang::SVInt(type.getBitWidth(), uint64_t(loops[i].at(remainder % loops[i].count)), type.isSigned());
            remainder /= loops[i].count;
        }
        auto ranges = selectedRanges(*write.targetExpr, *write.target, ec);
        if (!ranges) return std::nullopt;
        result.insert(result.end(), ranges->begin(), ranges->end());
    }
    return result;
}
/// The bits `write` certainly leaves when the statement `base` (a reset branch; the whole procedure when null) runs to its end: the
/// bits `writeRanges` selects, when nothing between `base` and the write can stop it. A branch (`if`, except a lone `if` on an
/// elaboration constant that is true, `case`), a `while`, `do` or
/// `repeat` loop, a `for` or `foreach` loop whose iterations are not proven to be at least one, and an earlier `return`, `disable`,
/// `break` or `continue` of the block all mean the write may not run. Nothing then: the caller treats the write as no proof. With `asserted` (the
/// test of a disjunctive reset), a condition that is constant while its net is asserted and selects the write's arm is no branch.
inline std::optional<WriteRanges> certainRanges(const model::ProceduralWrite& write, Lint& l,
                                               const slang::ast::Statement* base = nullptr,
                                               const model::ResetBranch* asserted = nullptr) {
    if (write.afterPossibleExit) return std::nullopt;
    auto first = write.scopes.begin();
    if (base) {
        first = std::find(write.scopes.begin(), write.scopes.end(), base);
        if (first == write.scopes.end()) return std::nullopt;
        ++first;
    }
    for (auto it = first; it != write.scopes.end(); ++it) {
        const auto& scope = **it;
        if (scope.kind == slang::ast::StatementKind::ForLoop) {
            if (!constantLoopValues(scope.as<slang::ast::ForLoopStatement>(), l)) return std::nullopt;
        } else if (scope.kind == slang::ast::StatementKind::ForeachLoop) {
            for (const auto& dim : scope.as<slang::ast::ForeachLoopStatement>().loopDims)
                if (!dim.range || dim.range->width() == 0) return std::nullopt;   // a dynamic dimension may be empty
        } else if (scope.kind == slang::ast::StatementKind::Conditional) {
            // An `if` on an elaboration constant (a genvar test) that is true, with no `else`, is not a branch: the write always runs.
            // Its branch is the next scope.
            const auto& c = scope.as<slang::ast::ConditionalStatement>();
            // In the branch of a disjunctive reset test, read with the tested net held asserted: a condition that is constant then and
            // selects the write's arm is not a branch (`if (rst) q <= 0;` inside `rst || load`).
            if (asserted && write.process && c.conditions.size() == 1 && !c.conditions[0].pattern && it + 1 != write.scopes.end()) {
                const slang::ast::Statement* arm = *(it + 1);
                const bool inThen = arm == &c.ifTrue;
                if (inThen || arm == c.ifFalse)
                    if (auto truth = model::conditionWhileAsserted(*c.conditions[0].expr, *write.process, *asserted))
                        if (*truth == inThen) { ++it; continue; }
            }
            if (c.ifFalse || c.conditions.size() != 1 || c.conditions[0].pattern || callsSubroutine(*c.conditions[0].expr)) return std::nullopt;
            auto cv = l.evalConst(*c.conditions[0].expr);
            if (!cv.isInteger() || cv.integer().hasUnknown() || !cv.isTrue()) return std::nullopt;
            if (it + 1 != write.scopes.end() && *(it + 1) == &c.ifTrue) ++it;
        } else {
            return std::nullopt;
        }
    }
    return writeRanges(write, l);
}
/// `from` without the bits of `cut`.
inline WriteRanges subtractRanges(const WriteRanges& from, const WriteRanges& cut) {
    WriteRanges result;
    for (auto [lo, hi] : from) {
        std::vector<std::pair<uint64_t, uint64_t>> pieces{{lo, hi}};
        for (auto [a, b] : cut) {
            std::vector<std::pair<uint64_t, uint64_t>> next;
            for (auto [x, y] : pieces) {
                if (b < x || a > y) { next.emplace_back(x, y); continue; }
                if (a > x) next.emplace_back(x, a - 1);
                if (b < y) next.emplace_back(b + 1, y);
            }
            pieces = std::move(next);
        }
        result.insert(result.end(), pieces.begin(), pieces.end());
    }
    return result;
}
inline bool rangesCover(WriteRanges covering, const WriteRanges& required) {
    std::sort(covering.begin(), covering.end());
    for (auto [lo, hi] : required) {
        bool covered = false;
        for (auto [a, b] : covering) {
            if (a > lo) break;
            if (b >= hi) { covered = true; break; }
            if (b >= lo) lo = b + 1;
        }
        if (!covered) return false;
    }
    return true;
}
}
