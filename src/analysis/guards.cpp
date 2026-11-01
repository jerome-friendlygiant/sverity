#include "analysis/guards.h"

#include <optional>

#include "slang/ast/EvalContext.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/numeric/SVInt.h"

namespace sverity::flow {

using namespace slang;
using namespace slang::ast;

bool isPureRef(const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    switch (e.kind) {
        case ExpressionKind::NamedValue:
        case ExpressionKind::HierarchicalValue:
        case ExpressionKind::IntegerLiteral:
        case ExpressionKind::UnbasedUnsizedIntegerLiteral:
            return true;
        case ExpressionKind::ElementSelect: {
            auto& s = e.as<ElementSelectExpression>();
            return isPureRef(s.value()) && isPureRef(s.selector());
        }
        case ExpressionKind::RangeSelect: {
            auto& s = e.as<RangeSelectExpression>();
            return isPureRef(s.value()) && isPureRef(s.left()) && isPureRef(s.right());
        }
        case ExpressionKind::MemberAccess:
            return isPureRef(e.as<MemberAccessExpression>().value());
        default:
            return false;
    }
}

namespace {

// Two-state integer constant that fits int64 (a compile-time constant; not a runtime value).
std::optional<int64_t> constSmall(EvalContext& ec, const Expression& e) {
    ConstantValue cv = e.eval(ec);
    if (!cv.isInteger()) return std::nullopt;
    const SVInt& v = cv.integer();
    if (v.hasUnknown() || v.getMinRepresentedBits() > 63) return std::nullopt;
    return v.as<int64_t>();
}

bool isRuntime(EvalContext& ec, const Expression& e) { return e.eval(ec).bad(); }

// "operand == c" (eq) or "operand != c" (!eq); a bare operand is operand != 0 and !operand is operand == 0.
struct CondForm {
    const Expression* operand;
    bool eq;
    int64_t c;
};

std::optional<CondForm> condForm(EvalContext& ec, const Expression& e0) {
    const Expression& e = e0.unwrapImplicitConversions();
    if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
        const Expression& o = e.as<UnaryExpression>().operand().unwrapImplicitConversions();
        if (isPureRef(o) && isRuntime(ec, o)) return CondForm{&o, true, 0};
        return std::nullopt;
    }
    if (e.kind == ExpressionKind::BinaryOp) {
        auto& bo = e.as<BinaryExpression>();
        bool eq = bo.op == BinaryOperator::Equality || bo.op == BinaryOperator::CaseEquality;
        bool ne = bo.op == BinaryOperator::Inequality || bo.op == BinaryOperator::CaseInequality;
        if (!eq && !ne) return std::nullopt;
        const Expression& lhs = bo.left().unwrapImplicitConversions();
        const Expression& rhs = bo.right().unwrapImplicitConversions();
        if (isPureRef(lhs) && isRuntime(ec, lhs))
            if (auto c = constSmall(ec, rhs)) return CondForm{&lhs, eq, *c};
        if (isPureRef(rhs) && isRuntime(ec, rhs))
            if (auto c = constSmall(ec, lhs)) return CondForm{&rhs, eq, *c};
        return std::nullopt;
    }
    if (isPureRef(e) && isRuntime(ec, e)) return CondForm{&e, false, 0};
    return std::nullopt;
}

} // namespace

bool conditionsExclusive(EvalContext& ec, const Expression& e1, bool t1, const Expression& e2, bool t2) {
    auto a = condForm(ec, e1), b = condForm(ec, e2);
    if (!a || !b || !a->operand->isEquivalentTo(*b->operand)) return false;
    bool aeq = t1 ? a->eq : !a->eq, beq = t2 ? b->eq : !b->eq;
    return (aeq && beq && a->c != b->c) || (aeq != beq && a->c == b->c);
}

bool GuardMap::condsExclusive(const Expression& e1, bool t1, const Expression& e2, bool t2,
                              std::vector<const Expression*>* operands) const {
    auto a = condForm(ec_, e1), b = condForm(ec_, e2);
    if (!a || !b || !a->operand->isEquivalentTo(*b->operand)) return false;
    bool aeq = t1 ? a->eq : !a->eq, beq = t2 ? b->eq : !b->eq;   // a not-taken branch negates the comparison
    bool excl = (aeq && beq && a->c != b->c) || (aeq != beq && a->c == b->c);
    if (excl && operands) operands->push_back(a->operand);
    return excl;
}

GuardMap::GuardMap(const Statement& root, EvalContext& ec) : ec_(ec) { walk(root, {}); }

void GuardMap::walk(const Statement& s, GuardList cur) {
    map_[&s] = cur;
    switch (s.kind) {
        case StatementKind::List:
            for (auto* x : s.as<StatementList>().list) walk(*x, cur);
            break;
        case StatementKind::Block:
            walk(s.as<BlockStatement>().body, cur);
            break;
        case StatementKind::Timed:
            walk(s.as<TimedStatement>().stmt, cur);
            break;
        case StatementKind::Conditional: {
            auto& c = s.as<ConditionalStatement>();
            GuardList t = cur;
            for (auto& cond : c.conditions) t.push_back({Guard::Kind::If, true, cond.expr.get(), &s, 0});
            walk(c.ifTrue, std::move(t));
            if (c.ifFalse) {
                // `else` holds when not all of the conditions hold; only a single condition is a plain negated literal
                GuardList f = cur;
                const Expression* single = c.conditions.size() == 1 ? c.conditions[0].expr.get() : nullptr;
                f.push_back({Guard::Kind::If, false, single, &s, 0});
                walk(*c.ifFalse, std::move(f));
            }
            break;
        }
        case StatementKind::Case: {
            auto& c = s.as<CaseStatement>();
            uint32_t i = 0;
            for (auto& item : c.items) {
                GuardList g = cur;
                g.push_back({Guard::Kind::CaseArm, true, nullptr, &s, i++});
                walk(*item.stmt, std::move(g));
            }
            if (c.defaultCase) {
                GuardList g = cur;
                g.push_back({Guard::Kind::CaseDefault, true, nullptr, &s, i});
                walk(*c.defaultCase, std::move(g));
            }
            break;
        }
        case StatementKind::ForLoop:
        case StatementKind::WhileLoop:
        case StatementKind::DoWhileLoop:
        case StatementKind::ForeverLoop:
        case StatementKind::RepeatLoop:
        case StatementKind::ForeachLoop: {
            const Statement* body = nullptr;
            switch (s.kind) {
                case StatementKind::ForLoop: body = &s.as<ForLoopStatement>().body; break;
                case StatementKind::WhileLoop: body = &s.as<WhileLoopStatement>().body; break;
                case StatementKind::DoWhileLoop: body = &s.as<DoWhileLoopStatement>().body; break;
                case StatementKind::ForeverLoop: body = &s.as<ForeverLoopStatement>().body; break;
                case StatementKind::RepeatLoop: body = &s.as<RepeatLoopStatement>().body; break;
                default: body = &s.as<ForeachLoopStatement>().body; break;
            }
            cur.push_back({Guard::Kind::Loop, true, nullptr, &s, 0});
            walk(*body, std::move(cur));
            break;
        }
        default:
            break;
    }
}

bool GuardMap::exclusive(const GuardList& a, const GuardList& b, std::vector<const Expression*>* operands) const {
    for (auto& x : a) {
        for (auto& y : b) {
            if (x.owner && x.owner == y.owner) {
                if (x.kind == Guard::Kind::If && y.kind == Guard::Kind::If && x.taken != y.taken) return true;
                bool xc = x.kind == Guard::Kind::CaseArm || x.kind == Guard::Kind::CaseDefault;
                bool yc = y.kind == Guard::Kind::CaseArm || y.kind == Guard::Kind::CaseDefault;
                if (xc && yc && (x.kind != y.kind || x.arm != y.arm)) return true;
            }
            if (x.kind == Guard::Kind::If && y.kind == Guard::Kind::If && x.cond && y.cond &&
                condsExclusive(*x.cond, x.taken, *y.cond, y.taken, operands))
                return true;
        }
    }
    return false;
}

bool GuardMap::implies(const GuardList& inner, const GuardList& outer) {
    for (auto& o : outer) {
        bool found = false;
        for (auto& i : inner)
            if (i.owner == o.owner && i.kind == o.kind && i.taken == o.taken && i.arm == o.arm && i.cond == o.cond) {
                found = true;
                break;
            }
        if (!found) return false;
    }
    return true;
}

bool GuardMap::inLoop(const GuardList& g) const {
    for (auto& x : g)
        if (x.kind == Guard::Kind::Loop) return true;
    return false;
}

} // namespace sverity::flow
