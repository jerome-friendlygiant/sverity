// CDC protocol / structure recognition helpers used by the crossing analysis (see cdc_shared.h):
// Gray-coded pointers, binary counters, payloads qualified by a synchronised control, SDC exception lookup.
#include "rules/cdc/cdc_shared.h"

#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <functional>
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/text/SourceManager.h"

#include "rules/sync_attribute.h"

namespace sverity::cdc {

using namespace slang;
using namespace slang::ast;
using namespace sverity::model;

namespace {

const Expression& strip(const Expression& e) {
    const Expression* x = &e;
    while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
    return *x;
}

bool sameExpr(const Expression& a, const Expression& b) {
    // A symbol reference alone also matches two different selects of that symbol.
    const auto whole = [](const Expression& e) {
        return e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue;
    };
    auto* sa = a.getSymbolReference();
    auto* sb = b.getSymbolReference();
    if (sa && sa == sb && whole(a) && whole(b)) return true;
    return a.syntax && b.syntax && a.syntax->toString() == b.syntax->toString();
}

bool mentionsSymbol(const Expression& e, const ValueSymbol& symbol) {
    struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
        const ValueSymbol& symbol;
        bool found = false;
        explicit Find(const ValueSymbol& s) : symbol(s) {}
        void handle(const NamedValueExpression& n) { found |= n.getSymbolReference() == &symbol; }
        void handle(const HierarchicalValueExpression& n) { found |= n.getSymbolReference() == &symbol; }
    } find(symbol);
    e.visit(find);
    return find.found;
}

// This exemption proves temporal monotonicity, not merely monotonicity of a
// Boolean function in q. q|x can only rise and q&x can only fall. Other forms,
// mixed directions, and writes that do not retain q are conservatively unknown.
enum class MonotonicDirection { Hold, Up, Down, Unknown };
MonotonicDirection monotonicSelfWrite(const Expression& raw, const ValueSymbol& symbol) {
    const Expression& e = strip(raw);
    if (!mentionsSymbol(e, symbol)) return MonotonicDirection::Unknown;
    if ((e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue) &&
        e.getSymbolReference() == &symbol) return MonotonicDirection::Hold;
    if (e.kind != ExpressionKind::BinaryOp) return MonotonicDirection::Unknown;
    const auto& b = e.as<BinaryExpression>();
    const bool leftSelf = mentionsSymbol(b.left(), symbol), rightSelf = mentionsSymbol(b.right(), symbol);
    if (leftSelf == rightSelf) return MonotonicDirection::Unknown;
    if (b.op == BinaryOperator::BinaryOr && (leftSelf ? b.left().getSymbolReference() == &symbol : b.right().getSymbolReference() == &symbol))
        return MonotonicDirection::Up;
    if (b.op == BinaryOperator::BinaryAnd && (leftSelf ? b.left().getSymbolReference() == &symbol : b.right().getSymbolReference() == &symbol))
        return MonotonicDirection::Down;
    return MonotonicDirection::Unknown;
}

bool monotonicSelfFeedback(const Model& model, NodeId q) {
    if (!model.selfLogic.count(q) || q >= model.nodes.size() || !model.nodes[q].sym) return false;
    const auto* symbol = model.nodes[q].sym;
    bool sawWrite = false;
    MonotonicDirection direction = MonotonicDirection::Unknown;
    for (const auto& write : model.proceduralWrites)
        if (std::find(write.mayWrite.begin(), write.mayWrite.end(), symbol) != write.mayWrite.end()) return false;
    for (const auto& write : model.systemWrites) if (write.target == symbol) return false;
    for (const auto& write : model.copyBackWrites) if (write.target == symbol) return false;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != symbol) continue;
        sawWrite = true;
        if (!write.value || write.called || write.partialTarget || !write.exactTarget) return false;
        const auto current = monotonicSelfWrite(*write.value, *symbol);
        if (current == MonotonicDirection::Unknown) return false;
        if (current != MonotonicDirection::Hold) {
            if (direction != MonotonicDirection::Unknown && direction != current) return false;
            direction = current;
        }
    }
    return sawWrite;
}

bool wholeReferenceTo(const Expression& raw, const ValueSymbol* symbol) {
    const auto& e = strip(raw);
    return symbol && (e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue) &&
           e.getSymbolReference() == symbol;
}

const Expression* stripNonNarrowing(const Expression& raw, uint64_t minWidth) {
    const Expression* x = &raw;
    while (x->kind == ExpressionKind::Conversion) {
        if (!x->type || x->type->getBitstreamWidth() < minWidth) return nullptr;
        x = &x->as<ConversionExpression>().operand();
    }
    if (!x->type || x->type->getBitstreamWidth() < minWidth) return nullptr;
    return x;
}

bool isUnnarrowedWholeReferenceTo(const Expression& raw, const ValueSymbol* symbol, uint64_t width) {
    if (!symbol) return false;
    const Expression* e = stripNonNarrowing(raw, width);
    return e && (e->kind == ExpressionKind::NamedValue || e->kind == ExpressionKind::HierarchicalValue) &&
           e->getSymbolReference() == symbol;
}

bool isExactWidthWholeReferenceTo(const Expression& raw, const ValueSymbol* symbol, uint64_t width) {
    if (!symbol) return false;
    const Expression* x = &raw;
    while (x->kind == ExpressionKind::Conversion) {
        if (!x->type || x->type->getBitstreamWidth() != width) return false;
        x = &x->as<ConversionExpression>().operand();
    }
    if (!x->type || x->type->getBitstreamWidth() != width) return false;
    return (x->kind == ExpressionKind::NamedValue || x->kind == ExpressionKind::HierarchicalValue) &&
           x->getSymbolReference() == symbol;
}

bool stageOperand(const Expression& raw, const Model& model, NodeId node) {
    if (node >= model.nodes.size()) return false;
    const auto& e = strip(raw);
    const auto* sym = model.nodes[node].sym;
    if (!sym) return false;
    if (wholeReferenceTo(e, sym)) return sym->getType().getBitWidth() == 1;
    if (e.kind != ExpressionKind::ElementSelect) return false;
    const auto& select = e.as<ElementSelectExpression>();
    if (strip(select.value()).getSymbolReference() != sym) return false;
    int64_t indexValue = 0;
    const ConstantValue* cv = select.selector().getConstant();
    if (cv && cv->isInteger() && !cv->integer().hasUnknown()) {
        auto index = cv->integer().as<int64_t>();
        if (!index) return false;
        indexValue = *index;
    } else if (select.selector().syntax) {
        const auto text = select.selector().syntax->toString();
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), indexValue);
        if (ec != std::errc{} || end != text.data() + text.size()) return false;
    } else return false;
    if (indexValue < 0) return false;
    const uint64_t width = sym->getType().getBitWidth();
    const uint64_t idx = static_cast<uint64_t>(indexValue);
    const uint64_t bit = model.nodes[node].bit;
    return width && idx < width && idx == bit;
}

bool exactComparison(const Expression& raw, const Model& model, NodeId stage, NodeId next, bool* activeLow = nullptr) {
    const auto& e = strip(raw);
    if (e.kind != ExpressionKind::BinaryOp) return false;
    const auto& b = e.as<BinaryExpression>();
    const bool match = (b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality) &&
        ((stageOperand(b.left(), model, stage) && stageOperand(b.right(), model, next)) ||
         (stageOperand(b.left(), model, next) && stageOperand(b.right(), model, stage)));
    if (match && activeLow) *activeLow = (b.op == BinaryOperator::Equality);
    return match;
}

bool influencesStage(const Model& model, const Expression& expr, NodeId stage, NodeId next) {
    if (stage >= model.nodes.size() || next >= model.nodes.size()) return true;
    std::unordered_set<const ValueSymbol*> refs;
    struct Refs : ASTVisitor<Refs, VisitFlags::AllGood> {
        std::unordered_set<const ValueSymbol*>& out;
        bool opaque = false;
        explicit Refs(std::unordered_set<const ValueSymbol*>& o) : out(o) {}
        void handle(const NamedValueExpression& e) {
            if (auto* s = e.getSymbolReference(); s && s->isValue()) out.insert(&s->as<ValueSymbol>());
        }
        void handle(const HierarchicalValueExpression& e) {
            if (auto* s = e.getSymbolReference(); s && s->isValue()) out.insert(&s->as<ValueSymbol>());
        }
        void handle(const CallExpression&) { opaque = true; }
    } refsVisitor(refs);
    expr.visit(refsVisitor);
    if (refsVisitor.opaque) return true;
    std::vector<NodeId> todo;
    for (NodeId n = 0; n < model.nodes.size(); ++n)
        if (refs.count(model.nodes[n].sym)) todo.push_back(n);
    std::unordered_set<NodeId> seen;
    while (!todo.empty()) {
        NodeId n = todo.back(); todo.pop_back();
        if (!seen.insert(n).second) continue;
        if (n == stage || n == next) return true;
        for (auto ei : model.in[n]) {
            const auto& edge = model.edges[ei];
            if (edge.type == EdgeType::Comb) todo.push_back(edge.from);
        }
    }
    return false;
}

bool qualifierSatisfiable(const Expression& expr, const Model& model, NodeId stage, bool bitwiseAnd) {
    const Scope* scope = stage < model.nodes.size() && model.nodes[stage].sym
        ? model.nodes[stage].sym->getParentScope() : nullptr;
    auto evalInt = [&](const Expression& e) -> std::optional<SVInt> {
        if (const auto* cv = e.getConstant(); cv && cv->isInteger()) return cv->integer();
        if (!scope) return std::nullopt;
        ASTContext ac(*scope, LookupLocation::max);
        EvalContext ec(ac);
        const auto cv = e.eval(ec);
        if (cv.isInteger()) return cv.integer();
        return std::nullopt;
    };
    if (auto iv = evalInt(expr)) {
        if (iv->hasUnknown()) return false;
        if (bitwiseAnd) return iv->getBitWidth() > 0 && (*iv)[0] == slang::logic_t(true);
        return static_cast<bool>(!(*iv == 0));
    }
    struct QFormula {
        enum class Kind { Const, Atom, Not, And, Or, Xor, Equal };
        Kind kind = Kind::Atom;
        bool val = false;
        size_t atom = 0;
        std::vector<std::shared_ptr<QFormula>> args;
    };
    using QF = std::shared_ptr<QFormula>;
    auto mkF = [](QFormula::Kind k, bool v = false, size_t a = 0, std::vector<QF> args = {}) {
        auto f = std::make_shared<QFormula>();
        f->kind = k; f->val = v; f->atom = a; f->args = std::move(args);
        return f;
    };
    std::unordered_map<NodeId, size_t> nodeAtoms;
    std::unordered_map<std::string, size_t> exprAtoms;
    struct EqConstraint {
        size_t atom = 0;
        std::string exprKey;
        SVInt val;
    };
    std::vector<EqConstraint> eqConstraints;
    size_t atomCount = 0;
    bool tooManyAtoms = false;
    auto hasCallInExpr = [](const Expression& e) {
        struct FindCall : ASTVisitor<FindCall, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression&) { found = true; }
        } fc;
        e.visit(fc);
        return fc.found;
    };
    auto normalizeExprKey = [&](const Expression& raw) -> std::string {
        const Expression& s = strip(raw);
        if (s.kind == ExpressionKind::NamedValue || s.kind == ExpressionKind::HierarchicalValue) {
            if (const auto* ref = s.getSymbolReference())
                return "sym:" + std::to_string(reinterpret_cast<uintptr_t>(ref));
        }
        if (!s.syntax) return {};
        std::string rawText = s.syntax->toString();
        std::string out;
        out.reserve(rawText.size());
        for (unsigned char c : rawText)
            if (!std::isspace(c)) out.push_back(static_cast<char>(c));
        while (out.size() >= 2 && out.front() == '(' && out.back() == ')') {
            int depth = 0;
            bool wrapsWhole = true;
            for (size_t i = 0; i + 1 < out.size(); ++i) {
                if (out[i] == '(') ++depth;
                else if (out[i] == ')') --depth;
                if (depth == 0) { wrapsWhole = false; break; }
            }
            if (!wrapsWhole) break;
            out = out.substr(1, out.size() - 2);
        }
        return out;
    };
    auto eqConstAtom = [&](const std::string& exprKey, const SVInt& val) -> QF {
        for (const auto& c : eqConstraints) {
            if (c.exprKey == exprKey && static_cast<bool>(c.val == val))
                return mkF(QFormula::Kind::Atom, false, c.atom);
        }
        if (atomCount >= 12) { tooManyAtoms = true; return nullptr; }
        const size_t id = atomCount++;
        eqConstraints.push_back({id, exprKey, val});
        return mkF(QFormula::Kind::Atom, false, id);
    };
    auto atomForNode = [&](NodeId n) -> QF {
        bool inv = false;
        for (int step = 0; step < 32; ++step) {
            const char cv = model.constantValue(n);
            if (cv == '0' || cv == '1') {
                const bool bit = (cv == '1') != inv;
                return mkF(QFormula::Kind::Const, bit);
            }
            if (n >= model.in.size() || model.in[n].size() != 1) break;
            const Edge& e = model.edges[model.in[n][0]];
            if (e.type != EdgeType::Comb || e.ctrl || e.approximate || !(e.exact || e.flip)) break;
            n = e.from;
            if (e.flip) inv = !inv;
        }
        auto [it, fresh] = nodeAtoms.try_emplace(n, atomCount);
        if (fresh) {
            if (atomCount >= 12) { tooManyAtoms = true; return nullptr; }
            ++atomCount;
        }
        QF base = mkF(QFormula::Kind::Atom, false, it->second);
        return inv ? mkF(QFormula::Kind::Not, false, 0, {base}) : base;
    };
    auto opaqueAtom = [&](const Expression& e) -> QF {
        std::string key = normalizeExprKey(e);
        if (key.empty()) {
            if (atomCount >= 12) { tooManyAtoms = true; return nullptr; }
            return mkF(QFormula::Kind::Atom, false, atomCount++);
        }
        auto [it, fresh] = exprAtoms.try_emplace(std::move(key), atomCount);
        if (fresh) {
            if (atomCount >= 12) { tooManyAtoms = true; return nullptr; }
            ++atomCount;
        }
        return mkF(QFormula::Kind::Atom, false, it->second);
    };
    auto findScalarNode = [&](const Expression& e) -> std::optional<NodeId> {
        const auto& s = strip(e);
        if (!s.type || s.type->getBitstreamWidth() != 1) return std::nullopt;
        const ValueSymbol* sym = nullptr;
        uint32_t bit = 0;
        if (s.kind == ExpressionKind::NamedValue || s.kind == ExpressionKind::HierarchicalValue) {
            const auto* ref = s.getSymbolReference();
            sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
            if (!sym || sym->getType().getBitWidth() != 1) return std::nullopt;
        } else if (s.kind == ExpressionKind::ElementSelect) {
            const auto& sel = s.as<ElementSelectExpression>();
            const auto* ref = strip(sel.value()).getSymbolReference();
            sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
            auto idx = evalInt(sel.selector());
            if (!sym || !idx || idx->hasUnknown()) return std::nullopt;
            auto iv = idx->as<int64_t>();
            if (!iv || *iv < 0 || static_cast<uint64_t>(*iv) >= sym->getType().getBitWidth()) return std::nullopt;
            bit = static_cast<uint32_t>(*iv);
        } else return std::nullopt;
        for (NodeId n = 0; n < model.nodes.size(); ++n)
            if (model.nodes[n].sym == sym && model.nodes[n].bit == bit) return n;
        return std::nullopt;
    };
    std::function<QF(const Expression&, bool)> toFormula = [&](const Expression& raw, bool bit0) -> QF {
        if (auto iv = evalInt(raw)) {
            if (iv->hasUnknown()) return mkF(QFormula::Kind::Const, false);
            if (bit0) return mkF(QFormula::Kind::Const, iv->getBitWidth() > 0 && (*iv)[0] == slang::logic_t(true));
            return mkF(QFormula::Kind::Const, static_cast<bool>(!(*iv == 0)));
        }
        const Expression& e = strip(raw);
        if (auto n = findScalarNode(e)) return atomForNode(*n);
        if (e.kind == ExpressionKind::UnaryOp) {
            const auto& u = e.as<UnaryExpression>();
            if (u.op == UnaryOperator::LogicalNot ||
                (u.op == UnaryOperator::BitwiseNot && u.operand().type && u.operand().type->getBitstreamWidth() == 1)) {
                QF inner = toFormula(u.operand(), false);
                return inner ? mkF(QFormula::Kind::Not, false, 0, {inner}) : nullptr;
            }
        }
        if (e.kind == ExpressionKind::BinaryOp) {
            const auto& b = e.as<BinaryExpression>();
            const bool isScalar = b.left().type && b.right().type &&
                                  b.left().type->getBitstreamWidth() == 1 &&
                                  b.right().type->getBitstreamWidth() == 1;
            if (!hasCallInExpr(b.left()) && !hasCallInExpr(b.right())) {
                const std::string lk = normalizeExprKey(b.left());
                const std::string rk = normalizeExprKey(b.right());
                if (!lk.empty() && lk == rk) {
                    switch (b.op) {
                        case BinaryOperator::BinaryXor:
                        case BinaryOperator::Subtract:
                        case BinaryOperator::Inequality:
                        case BinaryOperator::CaseInequality:
                        case BinaryOperator::LessThan:
                        case BinaryOperator::GreaterThan:
                            return mkF(QFormula::Kind::Const, false);
                        case BinaryOperator::Equality:
                        case BinaryOperator::CaseEquality:
                        case BinaryOperator::LessThanEqual:
                        case BinaryOperator::GreaterThanEqual:
                        case BinaryOperator::BinaryXnor:
                            return mkF(QFormula::Kind::Const, true);
                        default: break;
                    }
                }
                auto isComplementary = [&](const Expression& lhs, const Expression& rhs) {
                    const Expression& sx = strip(lhs);
                    if (sx.kind != ExpressionKind::UnaryOp) return false;
                    const auto& u = sx.as<UnaryExpression>();
                    if (u.op != UnaryOperator::BitwiseNot && u.op != UnaryOperator::LogicalNot) return false;
                    if (u.op == UnaryOperator::LogicalNot &&
                        (!rhs.type || rhs.type->getBitstreamWidth() != 1 ||
                         !u.operand().type || u.operand().type->getBitstreamWidth() != 1))
                        return false;
                    const std::string uk = normalizeExprKey(u.operand());
                    const std::string ok = normalizeExprKey(rhs);
                    return !uk.empty() && uk == ok;
                };
                if ((b.op == BinaryOperator::BinaryAnd || b.op == BinaryOperator::LogicalAnd) &&
                    (isComplementary(b.left(), b.right()) || isComplementary(b.right(), b.left())))
                    return mkF(QFormula::Kind::Const, false);
            }
            if (b.op == BinaryOperator::LogicalAnd || (b.op == BinaryOperator::BinaryAnd && (isScalar || bit0))) {
                QF l = toFormula(b.left(), b.op == BinaryOperator::BinaryAnd);
                QF r = toFormula(b.right(), b.op == BinaryOperator::BinaryAnd);
                if (l && l->kind == QFormula::Kind::Const && !l->val) return l;
                if (r && r->kind == QFormula::Kind::Const && !r->val) return r;
                if (!l || !r) return nullptr;
                return mkF(QFormula::Kind::And, false, 0, {l, r});
            }
            if (b.op == BinaryOperator::LogicalOr || (b.op == BinaryOperator::BinaryOr && (isScalar || bit0))) {
                QF l = toFormula(b.left(), b.op == BinaryOperator::BinaryOr);
                QF r = toFormula(b.right(), b.op == BinaryOperator::BinaryOr);
                if (!l || !r) return nullptr;
                return mkF(QFormula::Kind::Or, false, 0, {l, r});
            }
            if (isScalar && (b.op == BinaryOperator::BinaryXor || b.op == BinaryOperator::BinaryXnor)) {
                QF l = toFormula(b.left(), false), r = toFormula(b.right(), false);
                if (!l || !r) return nullptr;
                QF x = mkF(QFormula::Kind::Xor, false, 0, {l, r});
                return b.op == BinaryOperator::BinaryXnor ? mkF(QFormula::Kind::Not, false, 0, {x}) : x;
            }
            if (isScalar && (b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality ||
                             b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality)) {
                QF l = toFormula(b.left(), false), r = toFormula(b.right(), false);
                if (!l || !r) return nullptr;
                QF eq = mkF(QFormula::Kind::Equal, false, 0, {l, r});
                return (b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseInequality)
                    ? mkF(QFormula::Kind::Not, false, 0, {eq}) : eq;
            }
            if (b.op == BinaryOperator::Equality || b.op == BinaryOperator::Inequality ||
                b.op == BinaryOperator::CaseEquality || b.op == BinaryOperator::CaseInequality) {
                const bool isNeq = b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseInequality;
                auto lConst = evalInt(b.left());
                auto rConst = evalInt(b.right());
                if (rConst && !rConst->hasUnknown() && !hasCallInExpr(b.left())) {
                    const std::string key = normalizeExprKey(b.left());
                    if (!key.empty()) {
                        QF a = eqConstAtom(key, *rConst);
                        if (!a) return nullptr;
                        return isNeq ? mkF(QFormula::Kind::Not, false, 0, {a}) : a;
                    }
                } else if (lConst && !lConst->hasUnknown() && !hasCallInExpr(b.right())) {
                    const std::string key = normalizeExprKey(b.right());
                    if (!key.empty()) {
                        QF a = eqConstAtom(key, *lConst);
                        if (!a) return nullptr;
                        return isNeq ? mkF(QFormula::Kind::Not, false, 0, {a}) : a;
                    }
                }
            }
        }
        return opaqueAtom(e);
    };
    QF formula = toFormula(expr, bitwiseAnd);
    if (!formula) return !tooManyAtoms ? true : false;
    if (tooManyAtoms || atomCount > 12) return true;
    std::function<bool(const QF&, size_t)> evalFormula = [&](const QF& f, size_t vals) -> bool {
        switch (f->kind) {
            case QFormula::Kind::Const: return f->val;
            case QFormula::Kind::Atom: return ((vals >> f->atom) & 1) != 0;
            case QFormula::Kind::Not: return !evalFormula(f->args[0], vals);
            case QFormula::Kind::And: return evalFormula(f->args[0], vals) && evalFormula(f->args[1], vals);
            case QFormula::Kind::Or: return evalFormula(f->args[0], vals) || evalFormula(f->args[1], vals);
            case QFormula::Kind::Xor: return evalFormula(f->args[0], vals) != evalFormula(f->args[1], vals);
            case QFormula::Kind::Equal: return evalFormula(f->args[0], vals) == evalFormula(f->args[1], vals);
        }
        return false;
    };
    auto eqConsistent = [&](size_t vals) -> bool {
        for (size_t i = 0; i < eqConstraints.size(); ++i) {
            if (((vals >> eqConstraints[i].atom) & 1) == 0) continue;
            for (size_t j = i + 1; j < eqConstraints.size(); ++j) {
                if (((vals >> eqConstraints[j].atom) & 1) == 0) continue;
                if (eqConstraints[i].exprKey == eqConstraints[j].exprKey &&
                    !static_cast<bool>(eqConstraints[i].val == eqConstraints[j].val))
                    return false;
            }
        }
        return true;
    };
    for (size_t vals = 0; vals < (size_t(1) << atomCount); ++vals)
        if (eqConsistent(vals) && evalFormula(formula, vals)) return true;
    return false;
}

bool independentQualifier(const Expression& expr, const Model& model, NodeId stage, NodeId next, bool bitwiseAnd = false) {
    if (stage >= model.nodes.size() || next >= model.nodes.size()) return false;
    const auto* stageSym = model.nodes[stage].sym;
    const auto* nextSym = model.nodes[next].sym;
    return stageSym && nextSym && !influencesStage(model, expr, stage, next) &&
           qualifierSatisfiable(expr, model, stage, bitwiseAnd);
}

// Qualification may add a conjunction, but may not negate or override the
// event comparison. In particular this intentionally does not descend through
// XOR/OR, where an otherwise matching subtree can be cancelled or changed.
bool containsQualifiedComparison(const Expression& raw, const Model& model, NodeId stage, NodeId next, bool* activeLow = nullptr) {
    const auto& e = strip(raw);
    if (exactComparison(e, model, stage, next, activeLow)) return true;
    if (e.kind != ExpressionKind::BinaryOp) return false;
    const auto& b = e.as<BinaryExpression>();
    if (b.op != BinaryOperator::BinaryAnd && b.op != BinaryOperator::LogicalAnd) return false;
    const bool bw = b.op == BinaryOperator::BinaryAnd;
    return (containsQualifiedComparison(b.left(), model, stage, next, activeLow) && independentQualifier(b.right(), model, stage, next, bw)) ||
           (containsQualifiedComparison(b.right(), model, stage, next, activeLow) && independentQualifier(b.left(), model, stage, next, bw));
}

bool exactXor(const Expression& raw, const Model& model, NodeId stage, NodeId next, bool* activeLow = nullptr) {
    const auto& e = strip(raw);
    if (e.kind != ExpressionKind::BinaryOp) return false;
    const auto& b = e.as<BinaryExpression>();
    if (b.op == BinaryOperator::BinaryAnd || b.op == BinaryOperator::LogicalAnd) {
        const bool bw = b.op == BinaryOperator::BinaryAnd;
        return (exactXor(b.left(), model, stage, next, activeLow) && independentQualifier(b.right(), model, stage, next, bw)) ||
               (exactXor(b.right(), model, stage, next, activeLow) && independentQualifier(b.left(), model, stage, next, bw));
    }
    const bool match = (b.op == BinaryOperator::BinaryXor || b.op == BinaryOperator::BinaryXnor) &&
        ((stageOperand(b.left(), model, stage) && stageOperand(b.right(), model, next)) ||
         (stageOperand(b.left(), model, next) && stageOperand(b.right(), model, stage)));
    if (match && activeLow) *activeLow = (b.op == BinaryOperator::BinaryXnor);
    return match;
}

bool invertedStageOperand(const Expression& raw, const Model& model, NodeId node) {
    const auto& e = strip(raw);
    if (e.kind != ExpressionKind::UnaryOp) return false;
    const auto& u = e.as<UnaryExpression>();
    if (u.op != UnaryOperator::LogicalNot &&
        !(u.op == UnaryOperator::BitwiseNot && e.type && e.type->getBitstreamWidth() == 1))
        return false;
    return stageOperand(u.operand(), model, node);
}

bool exactInvertedAnd(const Expression& raw, const Model& model, NodeId stage, NodeId next, bool* activeLow = nullptr) {
    const auto& e = strip(raw);
    if (e.kind != ExpressionKind::BinaryOp) return false;
    const auto& b = e.as<BinaryExpression>();
    if (b.op != BinaryOperator::BinaryAnd && b.op != BinaryOperator::LogicalAnd) return false;
    const bool bw = b.op == BinaryOperator::BinaryAnd;
    if ((exactInvertedAnd(b.left(), model, stage, next, activeLow) && independentQualifier(b.right(), model, stage, next, bw)) ||
        (exactInvertedAnd(b.right(), model, stage, next, activeLow) && independentQualifier(b.left(), model, stage, next, bw)))
        return true;
    const bool match =
        (stageOperand(b.left(), model, stage) && invertedStageOperand(b.right(), model, next)) ||
        (invertedStageOperand(b.left(), model, stage) && stageOperand(b.right(), model, next)) ||
        (stageOperand(b.left(), model, next) && invertedStageOperand(b.right(), model, stage)) ||
        (invertedStageOperand(b.left(), model, next) && stageOperand(b.right(), model, stage));
    if (match && activeLow) *activeLow = false;
    return match;
}

bool matchesResetArm(const Model& model, NodeId target, const Statement* scope, const Statement* arm,
                     bool asserted, bool asyncOnly = false) {
    if (scope->kind != StatementKind::Conditional) return false;
    const auto& guard = scope->as<ConditionalStatement>();
    if (guard.conditions.size() != 1) return false;
    const auto* expr = &strip(*guard.conditions.front().expr);
    bool negated = false;
    if (expr->kind == ExpressionKind::UnaryOp) {
        const auto& unary = expr->as<UnaryExpression>();
        if (unary.op != UnaryOperator::LogicalNot && unary.op != UnaryOperator::BitwiseNot) return false;
        expr = &strip(unary.operand());
        negated = true;
    } else if (expr->kind == ExpressionKind::BinaryOp) {
        const auto& binary = expr->as<BinaryExpression>();
        if (binary.op != BinaryOperator::Equality && binary.op != BinaryOperator::Inequality &&
            binary.op != BinaryOperator::CaseEquality && binary.op != BinaryOperator::CaseInequality) return false;
        auto constBit = [&](const Expression& e) -> std::optional<bool> {
            const auto& s = strip(e);
            if (const auto* cv = s.getConstant(); cv && cv->isInteger() && !cv->integer().hasUnknown()) {
                if (cv->integer() == 0) return false;
                if (cv->integer() == 1) return true;
            }
            if (s.kind == ExpressionKind::IntegerLiteral) {
                const auto& iv = s.as<IntegerLiteral>().getValue();
                if (!iv.hasUnknown()) {
                    if (iv == 0) return false;
                    if (iv == 1) return true;
                }
            }
            return std::nullopt;
        };
        const bool isEq = binary.op == BinaryOperator::Equality || binary.op == BinaryOperator::CaseEquality;
        if (auto c = constBit(binary.right())) {
            expr = &strip(binary.left());
            negated = (!*c) == isEq;
        } else if (auto c = constBit(binary.left())) {
            expr = &strip(binary.right());
            negated = (!*c) == isEq;
        } else {
            return false;
        }
        if (!expr->type || expr->type->getBitstreamWidth() != 1) return false;
    }
    const auto* flop = model.flopAt(target);
    if (!flop) return false;
    const auto matches = [&](NodeId signal, bool activeLow) {
        if (signal >= model.nodes.size() || !wholeReferenceTo(*expr, model.nodes[signal].sym)) return false;
        const bool trueArmAsserts = negated == activeLow;
        return (asserted == trueArmAsserts ? &guard.ifTrue : guard.ifFalse) == arm;
    };
    if (!asyncOnly &&
        (std::any_of(flop->syncResetFacts.begin(), flop->syncResetFacts.end(), [&](const SyncResetGuard& fact) {
             return matches(fact.node, fact.activeLow);
         }) || matches(flop->syncReset, flop->syncActiveLow)))
        return true;
    return std::any_of(flop->resets.begin(), flop->resets.end(), [&](const ResetInfo& reset) {
        return matches(reset.signal, reset.activeLow);
    });
}

// An event write can be unconditional, a detector arm, or either of
// those inside the inactive arm of one recognized simple reset.
bool detectorPath(const Model& model, const ProceduralWrite& write, NodeId target, size_t detectorScopes) {
    if (write.afterPossibleExit) return false;
    if (write.scopes.size() == detectorScopes) return true;
    return write.scopes.size() == detectorScopes + 2 &&
           matchesResetArm(model, target, write.scopes[0], write.scopes[1], false);
}

bool resetConstantWrite(const Model& model, const ProceduralWrite& write, NodeId target) {
    return write.wholeConstant && !write.afterPossibleExit && write.scopes.size() == 2 &&
           matchesResetArm(model, target, write.scopes[0], write.scopes[1], true);
}

// A comparison is a detector only when one actual condition expression
// compares these two stage objects. Per-source operator tags cannot establish
// that relationship: `(s2 == a) && (s3 == b)` gives both stage edges Compare.
bool hasExactStageComparisonImpl(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr) {
    if (target >= model.nodes.size() || stage >= model.nodes.size() || next >= model.nodes.size()) return false;
    const auto* stageSym = model.nodes[stage].sym;
    const auto* nextSym = model.nodes[next].sym;
    if (!stageSym || !nextSym) return false;
    const ProceduralWrite* candidate = nullptr;
    const ProceduralWrite* candidateGuard = nullptr;
    bool directCandidate = false;
    bool directActiveLow = false;
    size_t writeCount = 0;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != model.nodes[target].sym) continue;
        ++writeCount;
        // The inventory is syntactic, so reject uncertain/partial writes and
        // competing procedural drivers before using any matching expression.
        if (write.called || write.partialTarget || write.forkOwnership || !write.process) return false;
        if (candidate && candidate->process != write.process) return false;
        candidate = &write;
        bool activeLow = false;
        if (write.value && containsQualifiedComparison(*write.value, model, stage, next, &activeLow)) {
            if (!detectorPath(model, write, target, 0)) return false;
            directCandidate = true;
            directActiveLow = activeLow;
        }
        for (const Statement* scope : write.scopes) {
            if (!scope || scope->kind != StatementKind::Conditional) continue;
            for (const auto& condition : scope->as<ConditionalStatement>().conditions) {
                if (containsQualifiedComparison(*condition.expr, model, stage, next)) {
                    candidateGuard = &write;
                    break;
                }
            }
        }
    }
    if (!candidate) return false;
    if (directCandidate) {
        if (writeCount == 1) {
            if (idleValue) *idleValue = directActiveLow ? '1' : '0';
            return true;
        }
        size_t directWrites = 0;
        for (const auto& write : model.proceduralWrites) {
            if (write.target != model.nodes[target].sym) continue;
            if (write.value && containsQualifiedComparison(*write.value, model, stage, next, &directActiveLow)) {
                ++directWrites;
                continue;
            }
            if (!resetConstantWrite(model, write, target)) return false;
        }
        if (directWrites != 1) return false;
        if (idleValue) *idleValue = directActiveLow ? '1' : '0';
        return true;
    }
    if (!candidateGuard) return false;
    // A guarded detector only establishes an event if every write to the
    // output is in that same process and is controlled by the detector path.
    // This deliberately leaves complicated partial/opaque control flow unknown.
    std::set<std::string> branchValues;
    char trueArmVal = '?', falseArmVal = '?';
    bool guardActiveLow = false;
    const ConditionalStatement* detectorGuard = nullptr;
    size_t detectorBranchWrites = 0;
    size_t trueBranchWrites = 0, falseBranchWrites = 0;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != model.nodes[target].sym) continue;
        if (write.process != candidateGuard->process) return false;
        bool hasDetectorScope = false;
        bool trueArmWrite = false, falseArmWrite = false;
        std::optional<bool> pathEquality;
        for (size_t i = 0; i < write.scopes.size(); ++i) {
            const Statement* scope = write.scopes[i];
            if (!scope || scope->kind != StatementKind::Conditional) continue;
            const auto& conditional = scope->as<ConditionalStatement>();
            bool matching = false;
            bool condLow = false;
            for (const auto& condition : conditional.conditions) {
                bool low = false;
                if (containsQualifiedComparison(*condition.expr, model, stage, next, &low)) {
                    matching = true;
                    condLow = low;
                }
            }
            if (matching) {
                if (!detectorPath(model, write, target, 2) || (detectorGuard && detectorGuard != &conditional)) return false;
                detectorGuard = &conditional;
                guardActiveLow = condLow;
                hasDetectorScope = true;
                if (conditional.conditions.size() != 1 || i + 2 != write.scopes.size()) return false;
                const auto* branch = write.scopes[i + 1];
                if (branch == &conditional.ifTrue) trueArmWrite = true;
                else if (conditional.ifFalse && branch == conditional.ifFalse) falseArmWrite = true;
                else return false;
            }
            // Other stage-derived guards can make the matching branch infeasible.
            // The inventory records each branch arm immediately after its if.
            if (!matching && !conditional.conditions.empty() && i + 1 < write.scopes.size() &&
                influencesStage(model, *conditional.conditions.front().expr, stage, next)) return false;
            if (!conditional.conditions.empty() && i + 1 < write.scopes.size()) {
                const auto& g = strip(*conditional.conditions.front().expr);
                if (g.kind == ExpressionKind::BinaryOp && exactComparison(g, model, stage, next)) {
                    const auto& branch = write.scopes[i + 1];
                    const bool trueArm = branch == &conditional.ifTrue;
                    const bool falseArm = conditional.ifFalse && branch == conditional.ifFalse;
                    if (trueArm || falseArm) {
                        const auto& binary = g.as<BinaryExpression>();
                        const bool equality = (binary.op == BinaryOperator::Equality) == trueArm;
                        if (pathEquality && *pathEquality != equality) return false;
                        pathEquality = equality;
                    }
                }
            }
            ++i; // the write inventory records the selected arm after its conditional
        }
        if (write.value && containsQualifiedComparison(*write.value, model, stage, next)) hasDetectorScope = true;
        if (!hasDetectorScope) {
            if (!resetConstantWrite(model, write, target)) return false;
            continue;
        }
        ++detectorBranchWrites;
        if (trueArmWrite == falseArmWrite) return false;
        trueBranchWrites += trueArmWrite;
        falseBranchWrites += falseArmWrite;
        if (!write.value || !write.wholeConstant) return false;
        EvalContext ec(*write.process);
        const auto value = write.value->eval(ec);
        if (!value.isInteger() || value.integer().hasUnknown() || value.integer().getBitWidth() != 1) return false;
        const char bitChar = value.isTrue() ? '1' : '0';
        if (trueArmWrite) trueArmVal = bitChar; else falseArmVal = bitChar;
        branchValues.insert(value.integer().toString());
    }
    if (!(detectorBranchWrites == 2 && trueBranchWrites == 1 && falseBranchWrites == 1 && branchValues.size() > 1))
        return false;
    if (idleValue) *idleValue = guardActiveLow ? trueArmVal : falseArmVal;
    return true;
}

template <typename ExprMatch>
bool hasExactStageExprDetector(const Model& model, NodeId target, NodeId stage, NodeId next,
                               ExprMatch&& matchExpr, char* idleValue = nullptr) {
    if (target >= model.nodes.size() || stage >= model.nodes.size() || next >= model.nodes.size()) return false;
    const ProceduralWrite* processWrite = nullptr;
    size_t directWrites = 0;
    bool direct = false;
    bool directActiveLow = false;
    bool guarded = false;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != model.nodes[target].sym) continue;
        if (write.called || write.partialTarget || write.forkOwnership || !write.process) return false;
        if (processWrite && processWrite->process != write.process) return false;
        processWrite = &write;
        bool activeLow = false;
        if (write.value && matchExpr(*write.value, model, stage, next, &activeLow)) {
            if (!detectorPath(model, write, target, 0)) return false;
            direct = true;
            directActiveLow = activeLow;
            ++directWrites;
        }
        for (const Statement* scope : write.scopes) {
            if (!scope || scope->kind != StatementKind::Conditional) continue;
            for (const auto& condition : scope->as<ConditionalStatement>().conditions)
                if (condition.expr && matchExpr(*condition.expr, model, stage, next, nullptr)) guarded = true;
        }
    }
    if (direct) {
        if (directWrites != 1) return false;
        bool foundDirect = false;
        for (const auto& write : model.proceduralWrites) {
            if (write.target != model.nodes[target].sym) continue;
            if (write.value && matchExpr(*write.value, model, stage, next, &directActiveLow)) { foundDirect = true; continue; }
            if (!resetConstantWrite(model, write, target)) return false;
        }
        if (!foundDirect) return false;
        if (idleValue) *idleValue = directActiveLow ? '1' : '0';
        return true;
    }
    if (!guarded || !processWrite) return false;
    std::set<std::string> branchValues;
    char trueArmVal = '?', falseArmVal = '?';
    bool guardActiveLow = false;
    const ConditionalStatement* detectorGuard = nullptr;
    size_t detectorBranchWrites = 0;
    size_t trueBranchWrites = 0, falseBranchWrites = 0;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != model.nodes[target].sym) continue;
        if (write.process != processWrite->process) return false;
        bool matchingGuard = false;
        bool trueArmWrite = false, falseArmWrite = false;
        for (size_t i = 0; i < write.scopes.size(); ++i) {
            const Statement* scope = write.scopes[i];
            if (!scope || scope->kind != StatementKind::Conditional) continue;
            const auto& conditional = scope->as<ConditionalStatement>();
            bool condLow = false;
            const bool thisGuard = std::any_of(conditional.conditions.begin(), conditional.conditions.end(),
                [&](const auto& condition) {
                    bool low = false;
                    if (condition.expr && matchExpr(*condition.expr, model, stage, next, &low)) {
                        condLow = low;
                        return true;
                    }
                    return false;
                });
            if (thisGuard) {
                if (matchingGuard) return false;
                if (!detectorPath(model, write, target, 2) || (detectorGuard && detectorGuard != &conditional)) return false;
                detectorGuard = &conditional;
                guardActiveLow = condLow;
                matchingGuard = true;
                if (conditional.conditions.size() != 1 || i + 2 != write.scopes.size()) return false;
                const auto* branch = write.scopes[i + 1];
                if (branch == &conditional.ifTrue) trueArmWrite = true;
                else if (conditional.ifFalse && branch == conditional.ifFalse) falseArmWrite = true;
                else return false;
            }
            if (!thisGuard && std::any_of(conditional.conditions.begin(), conditional.conditions.end(),
                [&](const auto& condition) { return influencesStage(model, *condition.expr, stage, next); })) return false;
            ++i; // the write inventory records the selected arm after its conditional
        }
        if (!matchingGuard) {
            if (resetConstantWrite(model, write, target)) continue;
            return false;
        }
        ++detectorBranchWrites;
        if (trueArmWrite == falseArmWrite) return false;
        trueBranchWrites += trueArmWrite;
        falseBranchWrites += falseArmWrite;
        if (!write.value || !write.wholeConstant) return false;
        EvalContext ec(*write.process);
        const auto value = write.value->eval(ec);
        if (!value.isInteger() || value.integer().hasUnknown() || value.integer().getBitWidth() != 1) return false;
        const char bitChar = value.isTrue() ? '1' : '0';
        if (trueArmWrite) trueArmVal = bitChar; else falseArmVal = bitChar;
        branchValues.insert(value.integer().toString());
    }
    if (!(detectorBranchWrites == 2 && trueBranchWrites == 1 && falseBranchWrites == 1 && branchValues.size() > 1))
        return false;
    if (idleValue) *idleValue = guardActiveLow ? trueArmVal : falseArmVal;
    return true;
}

bool hasExactCombinationalComparisonImpl(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                         NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr) {
    if (stage >= model.nodes.size() || next >= model.nodes.size() || nextEdge.proc != stageEdge.proc) return false;
    if (stageEdge.proc < model.procs.size() && model.procs[stageEdge.proc].kind == ProcKind::Comb)
        return hasExactStageComparisonImpl(model, target, stage, next, idleValue);
    for (const auto& fact : model.pairOperatorFacts) {
        bool activeLow = false;
        if (fact.proc == stageEdge.proc && fact.rhs &&
            containsQualifiedComparison(*fact.rhs, model, stage, next, &activeLow)) {
            if (idleValue) *idleValue = activeLow ? '1' : '0';
            return true;
        }
    }
    return false;
}

bool hasExactCombinationalXorImpl(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                  NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr) {
    if (stage >= model.nodes.size() || next >= model.nodes.size() || nextEdge.proc != stageEdge.proc) return false;
    if (stageEdge.proc < model.procs.size() && model.procs[stageEdge.proc].kind == ProcKind::Comb)
        return hasExactStageExprDetector(model, target, stage, next, exactXor, idleValue);
    for (const auto& fact : model.pairOperatorFacts) {
        bool activeLow = false;
        if (fact.proc == stageEdge.proc && fact.rhs && exactXor(*fact.rhs, model, stage, next, &activeLow)) {
            if (idleValue) *idleValue = activeLow ? '1' : '0';
            return true;
        }
    }
    return false;
}

bool hasExactCombinationalInvertedAndImpl(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                          NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr) {
    if (stage >= model.nodes.size() || next >= model.nodes.size() || nextEdge.proc != stageEdge.proc) return false;
    if (stageEdge.proc < model.procs.size() && model.procs[stageEdge.proc].kind == ProcKind::Comb)
        return hasExactStageExprDetector(model, target, stage, next, exactInvertedAnd, idleValue);
    for (const auto& fact : model.pairOperatorFacts) {
        bool activeLow = false;
        if (fact.proc == stageEdge.proc && fact.rhs && exactInvertedAnd(*fact.rhs, model, stage, next, &activeLow)) {
            if (idleValue) *idleValue = activeLow ? '1' : '0';
            return true;
        }
    }
    return false;
}

/// `x >> 1` (logical or arithmetic shift by the constant 1): returns x.
const Expression* shiftedByOne(const Expression& e, EvalContext& ec) {
    const Expression& x = strip(e);
    if (x.kind != ExpressionKind::BinaryOp) return nullptr;
    auto& b = x.as<BinaryExpression>();
    if (b.op != BinaryOperator::LogicalShiftRight && b.op != BinaryOperator::ArithmeticShiftRight) return nullptr;
    auto cv = b.right().eval(ec);
    if (!cv.isInteger() || cv.integer().hasUnknown() || cv.integer() != SVInt(32, 1, false)) return nullptr;
    return &b.left();
}

/// A single-bit select of a symbol with a constant index: returns (symbol, index).
bool constSelect(const Expression& e, EvalContext& ec, const ValueSymbol*& sym, int64_t& idx) {
    const Expression& x = strip(e);
    if (x.kind != ExpressionKind::ElementSelect) return false;
    auto& sel = x.as<ElementSelectExpression>();
    auto* ref = sel.value().getSymbolReference();
    sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
    auto cv = sel.selector().eval(ec);
    if (!sym || !cv.isInteger() || cv.integer().hasUnknown()) return false;
    auto v = cv.integer().as<int64_t>();
    if (!v) return false;
    idx = *v;
    return true;
}

/// binary-to-Gray: `b ^ (b >> 1)` in either operand order, or an XOR of two adjacent bit-selects of one vector.
/// Also accept the common exact implementation `{1'b0, b}[Width:1]`, where
/// the zero extension is assigned to a temporary in another module.
bool isBinaryToGray(const Expression& rhs, EvalContext& ec,
                    const std::unordered_map<const ValueSymbol*, const Expression*>& exactAliases) {
    const Expression& e = strip(rhs);
    if (e.kind != ExpressionKind::BinaryOp) return false;
    auto& b = e.as<BinaryExpression>();
    if (b.op != BinaryOperator::BinaryXor) return false;
    if (auto* x = shiftedByOne(b.right(), ec); x && sameExpr(*x, b.left())) return true;
    if (auto* x = shiftedByOne(b.left(), ec); x && sameExpr(*x, b.right())) return true;
    const ValueSymbol *s1 = nullptr, *s2 = nullptr;
    int64_t i1 = 0, i2 = 0;
    if (constSelect(b.left(), ec, s1, i1) && constSelect(b.right(), ec, s2, i2) && s1 == s2 && (i1 - i2 == 1 || i2 - i1 == 1)) return true;
    auto extendedSlice = [&](const Expression& vector, const Expression& slice) {
        const Expression& v = strip(vector);
        const Expression& r = strip(slice);
        if (v.kind != ExpressionKind::NamedValue || r.kind != ExpressionKind::RangeSelect) return false;
        auto* base = v.getSymbolReference() ? v.getSymbolReference()->as_if<ValueSymbol>() : nullptr;
        auto* temp = r.as<RangeSelectExpression>().value().getSymbolReference();
        auto* alias = temp ? temp->as_if<ValueSymbol>() : nullptr;
        if (!base || !alias) return false;
        auto found = exactAliases.find(alias);
        if (found == exactAliases.end()) return false;
        const Expression& ext = strip(*found->second);
        if (ext.kind != ExpressionKind::Concatenation) return false;
        const auto& parts = ext.as<ConcatenationExpression>().operands();
        if (parts.size() != 2 || strip(*parts[1]).kind != ExpressionKind::NamedValue ||
            strip(*parts[1]).getSymbolReference() != base) return false;
        auto zero = parts[0]->eval(ec);
        if (!zero.isInteger() || zero.integer().hasUnknown() || !zero.isFalse()) return false;
        const auto& range = r.as<RangeSelectExpression>();
        auto left = range.left().eval(ec), right = range.right().eval(ec);
        if (!left.isInteger() || left.integer().hasUnknown() || !right.isInteger() || right.integer().hasUnknown()) return false;
        auto hi = left.integer().as<int64_t>(), lo = right.integer().as<int64_t>();
        if (!alias->getType().isSimpleBitVector()) return false;
        const auto aliasRange = alias->getType().getFixedRange();
        if (!aliasRange.isDescending() || aliasRange.right != 0) return false;
        return hi && lo && *hi == static_cast<int64_t>(base->getType().getBitstreamWidth()) && *lo == 1 &&
               alias->getType().getBitstreamWidth() == base->getType().getBitstreamWidth() + 1;
    };
    return extendedSlice(b.left(), b.right()) || extendedSlice(b.right(), b.left());
}

// Only a single, side-effect-free function body that returns the same Gray
// expression accepted inline can be classified. In particular, a name is no evidence.
bool isGrayFunction(const Expression& expression, EvalContext& ec) {
    auto sameType = [](const Type& a, const Type& b) {
        return a.isIntegral() && b.isIntegral() && a.getBitstreamWidth() == b.getBitstreamWidth() && a.isSigned() == b.isSigned();
    };
    auto unwrap = [&](const Expression* x) -> const Expression* {
        while (x->kind == ExpressionKind::Conversion) {
            const Expression& operand = x->as<ConversionExpression>().operand();
            if (!sameType(*x->type, *operand.type)) return nullptr;
            x = &operand;
        }
        return x;
    };
    const Expression* x = unwrap(&expression);
    if (!x || x->kind != ExpressionKind::Call) return false;
    const auto& call = x->as<CallExpression>();
    if (call.isSystemCall() || call.thisClass() || call.hasOutputArgs() || call.arguments().size() != 1) return false;
    const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
    if (!sub || sub->getArguments().size() != 1 || !sameType(sub->getReturnType(), sub->getArguments()[0]->getType()) ||
        !sameType(sub->getReturnType(), *call.arguments()[0]->type)) return false;
    const Expression* argument = unwrap(call.arguments()[0]);
    if (!argument || argument->kind != ExpressionKind::NamedValue) return false;
    const Statement* body = &sub->getBody();
    while (body->kind == StatementKind::Block) body = &body->as<BlockStatement>().body;
    if (body->kind == StatementKind::List) {
        const auto& list = body->as<StatementList>().list;
        if (list.size() != 1) return false;
        body = list[0];
    }
    const Expression* result = nullptr;
    if (body->kind == StatementKind::Return) result = body->as<ReturnStatement>().expr;
    else if (body->kind == StatementKind::ExpressionStatement) {
        const auto& e = body->as<ExpressionStatement>().expr;
        if (e.kind != ExpressionKind::Assignment) return false;
        const auto& a = e.as<AssignmentExpression>();
        if (a.isNonBlocking() || a.isCompound() || a.left().getSymbolReference() != sub->returnValVar) return false;
        result = &a.right();
    }
    if (!result || !sameType(*result->type, sub->getReturnType())) return false;
    // Both occurrences must be the unmodified formal argument. No casts,
    // free variables, nested calls, or side effects can be hidden in them.
    const Expression* rhs = unwrap(result);
    if (!rhs || rhs->kind != ExpressionKind::BinaryOp) return false;
    const auto& xorExpr = rhs->as<BinaryExpression>();
    if (xorExpr.op != BinaryOperator::BinaryXor || !sameType(*xorExpr.left().type, sub->getReturnType()) ||
        !sameType(*xorExpr.right().type, sub->getReturnType())) return false;
    auto formal = [&](const Expression& e) {
        const Expression* v = unwrap(&e);
        return v && v->kind == ExpressionKind::NamedValue && v->getSymbolReference() == sub->getArguments()[0];
    };
    auto shiftedFormal = [&](const Expression& e) {
        const Expression* shift = unwrap(&e);
        if (!shift || shift->kind != ExpressionKind::BinaryOp) return false;
        const auto& op = shift->as<BinaryExpression>();
        if (op.op != BinaryOperator::LogicalShiftRight &&
            !(op.op == BinaryOperator::ArithmeticShiftRight && !sub->getArguments()[0]->getType().isSigned()))
            return false;
        // Restrict the shift count to a literal so evaluating it cannot hide
        // another call or side effect inside the purportedly pure function.
        if (strip(op.right()).kind != ExpressionKind::IntegerLiteral) return false;
        auto amount = op.right().eval(ec);
        return amount.isInteger() && !amount.integer().hasUnknown() && amount.integer() == SVInt(32, 1, false) && formal(op.left());
    };
    return (shiftedFormal(xorExpr.left()) && formal(xorExpr.right())) ||
           (shiftedFormal(xorExpr.right()) && formal(xorExpr.left()));
}

struct UserCallFinder : ASTVisitor<UserCallFinder, VisitFlags::AllGood> {
    bool found = false;
    void handle(const CallExpression& call) {
        if (!call.isSystemCall()) found = true;
        visitDefault(call);
    }
};

// A symbol is Gray only if all of its nonconstant writes meet the inline or
// proven-function predicate; mixed writes never inherit a Gray classification.
struct GrayFinder : ASTVisitor<GrayFinder, VisitFlags::AllGood> {
    std::unordered_set<const ValueSymbol*>& out;
    std::unordered_set<const ValueSymbol*>& opaque;
    std::unordered_set<const ValueSymbol*> grayWhole, other;
    struct Candidate {
        const ValueSymbol* sym;
        const Expression* lhs;
        const Expression* rhs;
        const ProceduralBlockSymbol* process;
        bool whole;
        bool guarded;
        bool inAsyncReset;
        std::vector<std::pair<const ConditionalStatement*, const Statement*>> condPath;
    };
    std::unordered_map<const ValueSymbol*, std::vector<size_t>> allWrites;
    std::vector<Candidate> candidates;
    Compilation& comp;
    const Model& model;
    std::unordered_map<const ValueSymbol*, NodeId> flopSymbolNode;
    const ProceduralBlockSymbol* process = nullptr;
    bool guarded = false;
    std::vector<std::pair<const ConditionalStatement*, const Statement*>> condStack;
    GrayFinder(std::unordered_set<const ValueSymbol*>& o, std::unordered_set<const ValueSymbol*>& u,
               Compilation& c, const Model& m)
        : out(o), opaque(u), comp(c), model(m) {
        for (const auto& flop : model.flops) {
            if (flop.q < model.nodes.size() && model.nodes[flop.q].sym)
                flopSymbolNode.try_emplace(model.nodes[flop.q].sym, flop.q);
        }
    }
    void handle(const ProceduralBlockSymbol& p) {
        const auto* saved = process;
        process = &p;
        visitDefault(p);
        process = saved;
    }
    void handle(const ConditionalStatement& branch) {
        if (process && branch.conditions.size() == 1 && !branch.conditions[0].pattern) {
            EvalContext context(*process);
            if (auto value = model::elaboratedCondition(*branch.conditions[0].expr, context)) {
                if (*value) branch.ifTrue.visit(*this);
                else if (branch.ifFalse) branch.ifFalse->visit(*this);
                return;
            }
        }
        const bool saved = guarded;
        guarded = true;
        for (const auto& cond : branch.conditions)
            if (cond.expr) cond.expr->visit(*this);
        condStack.push_back({&branch, &branch.ifTrue});
        branch.ifTrue.visit(*this);
        condStack.pop_back();
        if (branch.ifFalse) {
            condStack.push_back({&branch, branch.ifFalse});
            branch.ifFalse->visit(*this);
            condStack.pop_back();
        }
        guarded = saved;
    }
    template <typename T> void visitGuarded(const T& statement) {
        const bool saved = guarded;
        guarded = true;
        visitDefault(statement);
        guarded = saved;
    }
    void handle(const CaseStatement& statement) { visitGuarded(statement); }
    void handle(const ForLoopStatement& statement) { visitGuarded(statement); }
    void handle(const RepeatLoopStatement& statement) { visitGuarded(statement); }
    void handle(const ForeachLoopStatement& statement) { visitGuarded(statement); }
    void handle(const WhileLoopStatement& statement) { visitGuarded(statement); }
    void handle(const DoWhileLoopStatement& statement) { visitGuarded(statement); }
    void handle(const ForeverLoopStatement& statement) { visitGuarded(statement); }
    void inspect(const ValueSymbol* sym, const Expression* lhs, const Expression& rhs, bool whole) {
        bool inAsyncReset = false;
        if (auto it = flopSymbolNode.find(sym); it != flopSymbolNode.end()) {
            for (const auto& [cond, arm] : condStack) {
                if (matchesResetArm(model, it->second, cond, arm, true, /*asyncOnly=*/true)) {
                    inAsyncReset = true;
                    break;
                }
            }
        }
        allWrites[sym].push_back(candidates.size());
        candidates.push_back({sym, lhs, &rhs, process, whole, guarded, inAsyncReset, condStack});
    }
    void inspectAssignmentTarget(const Expression& lhs, const Expression& rhs, bool whole) {
        if (lhs.kind == ExpressionKind::Concatenation) {
            for (const auto* operand : lhs.as<ConcatenationExpression>().operands())
                inspectAssignmentTarget(*operand, rhs, false);
        } else if (lhs.kind == ExpressionKind::Streaming) {
            for (const auto& stream : lhs.as<StreamingConcatenationExpression>().streams())
                inspectAssignmentTarget(*stream.operand, rhs, false);
        } else if (const auto* ref = lhs.getSymbolReference()) {
            if (const auto* sym = ref->as_if<ValueSymbol>())
                inspect(sym, &lhs, rhs, whole && lhs.kind == ExpressionKind::NamedValue);
        }
    }
    void handle(const AssignmentExpression& a) {
        inspectAssignmentTarget(a.left(), a.right(), true);
        visitDefault(a);
    }
    void handle(const NetSymbol& net) {
        if (const auto* init = net.getInitializer()) inspect(&net, nullptr, *init, true);
        visitDefault(net);
    }
    void finish() {
        std::unordered_map<const ValueSymbol*, const Expression*> aliases;
        for (const auto& [sym, writes] : allWrites) {
            if (writes.size() != 1) continue;
            const auto& write = candidates[writes.front()];
            if (write.whole && !write.guarded && write.rhs->kind == ExpressionKind::Concatenation)
                aliases.emplace(sym, write.rhs);
        }
        // Reassemble complete, exact, nonoverlapping constant-index split non-reset Gray writes
        // from one sequential owner under compatible control (e.g. wr_gray[3] <= wr_bin[3]; wr_gray[2:0] <= wr_bin[2:0] ^ wr_bin[3:1];).
        std::unordered_set<const ValueSymbol*> reassembledGray;
        auto constInt = [](const Expression& e, EvalContext& ec) -> std::optional<int64_t> {
            auto v = e.eval(ec);
            if (v.bad() || !v.isInteger()) return std::nullopt;
            return v.integer().as<int64_t>();
        };
        auto extractSelect = [&](const Expression& expr, EvalContext& ec, const ValueSymbol*& outSym,
                                 int64_t& loNorm, int64_t& hiNorm, int64_t& widthOut) -> bool {
            const Expression& e = strip(expr);
            const ValueSymbol* sym = nullptr;
            int64_t lIdx = 0, rIdx = 0;
            if (e.kind == ExpressionKind::ElementSelect) {
                const auto& sel = e.as<ElementSelectExpression>();
                const auto* ref = sel.value().getSymbolReference();
                sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
                auto idx = constInt(sel.selector(), ec);
                if (!sym || !idx) return false;
                lIdx = rIdx = *idx;
            } else if (e.kind == ExpressionKind::RangeSelect) {
                const auto& sel = e.as<RangeSelectExpression>();
                if (sel.getSelectionKind() != RangeSelectionKind::Simple) return false;
                const auto* ref = sel.value().getSymbolReference();
                sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
                auto l = constInt(sel.left(), ec), r = constInt(sel.right(), ec);
                if (!sym || !l || !r) return false;
                lIdx = *l; rIdx = *r;
            } else return false;
            if (!sym->getType().isSimpleBitVector()) return false;
            const auto range = sym->getType().getFixedRange();
            const int64_t w = static_cast<int64_t>(range.width());
            if (w < 2) return false;
            auto toNorm = [&](int64_t idx) -> std::optional<int64_t> {
                int64_t n = range.isDescending() ? (idx - range.right) : (range.right - idx);
                if (n < 0 || n >= w) return std::nullopt;
                return n;
            };
            auto nl = toNorm(lIdx), nr = toNorm(rIdx);
            if (!nl || !nr) return false;
            outSym = sym;
            loNorm = std::min(*nl, *nr);
            hiNorm = std::max(*nl, *nr);
            widthOut = w;
            return true;
        };
        for (const auto& [sym, writeIdxs] : allWrites) {
            if (!flopSymbolNode.count(sym) || !sym->getType().isSimpleBitVector()) continue;
            const auto range = sym->getType().getFixedRange();
            const int64_t W = static_cast<int64_t>(range.width());
            if (W < 2) continue;
            std::vector<const Candidate*> splitWrites;
            bool eligible = true;
            const ProceduralBlockSymbol* ownerProc = nullptr;
            const std::vector<std::pair<const ConditionalStatement*, const Statement*>>* ownerCond = nullptr;
            for (size_t idx : writeIdxs) {
                const auto& c = candidates[idx];
                if (c.inAsyncReset) continue;
                if (!c.process) { eligible = false; break; }
                EvalContext ec(*c.process);
                if (!c.rhs->eval(ec).bad()) continue; // constant write (e.g. sync reset)
                const Expression& stripped = strip(*c.rhs);
                if (c.whole && stripped.kind == ExpressionKind::NamedValue && stripped.getSymbolReference() == sym)
                    continue; // whole self-hold
                if (c.whole || !c.lhs) { eligible = false; break; }
                if (!ownerProc) { ownerProc = c.process; ownerCond = &c.condPath; }
                else if (ownerProc != c.process || *ownerCond != c.condPath) { eligible = false; break; }
                splitWrites.push_back(&c);
            }
            if (!eligible || splitWrites.size() < 2 || !ownerProc) continue;
            EvalContext ec(*ownerProc);
            std::vector<bool> covered(static_cast<size_t>(W), false);
            const ValueSymbol* commonBin = nullptr;
            bool ok = true;
            for (const Candidate* c : splitWrites) {
                const ValueSymbol* lhsSym = nullptr;
                int64_t lo = 0, hi = 0, lw = 0;
                if (!extractSelect(*c->lhs, ec, lhsSym, lo, hi, lw) || lhsSym != sym || lw != W) { ok = false; break; }
                for (int64_t k = lo; k <= hi; ++k) {
                    if (covered[static_cast<size_t>(k)]) { ok = false; break; }
                    covered[static_cast<size_t>(k)] = true;
                }
                if (!ok) break;
                const Expression& rhs = strip(*c->rhs);
                const ValueSymbol* srcSym = nullptr;
                if (lo == W - 1 && hi == W - 1) {
                    // MSB: bin[MSB]
                    int64_t slo = 0, shi = 0, sw = 0;
                    if (!extractSelect(rhs, ec, srcSym, slo, shi, sw) || sw != W || slo != W - 1 || shi != W - 1) { ok = false; break; }
                } else if (hi < W - 1) {
                    // Bits [hi:lo]: bin[hi:lo] ^ bin[hi+1:lo+1] (in either order)
                    if (rhs.kind != ExpressionKind::BinaryOp || rhs.as<BinaryExpression>().op != BinaryOperator::BinaryXor) { ok = false; break; }
                    const auto& binOp = rhs.as<BinaryExpression>();
                    const ValueSymbol *s1 = nullptr, *s2 = nullptr;
                    int64_t lo1 = 0, hi1 = 0, w1 = 0, lo2 = 0, hi2 = 0, w2 = 0;
                    if (!extractSelect(binOp.left(), ec, s1, lo1, hi1, w1) ||
                        !extractSelect(binOp.right(), ec, s2, lo2, hi2, w2) ||
                        s1 != s2 || w1 != W) { ok = false; break; }
                    const bool matchFwd = (lo1 == lo && hi1 == hi && lo2 == lo + 1 && hi2 == hi + 1);
                    const bool matchRev = (lo2 == lo && hi2 == hi && lo1 == lo + 1 && hi1 == hi + 1);
                    if (!matchFwd && !matchRev) { ok = false; break; }
                    srcSym = s1;
                } else {
                    ok = false; break;
                }
                if (!commonBin) commonBin = srcSym;
                else if (commonBin != srcSym) { ok = false; break; }
            }
            if (ok && commonBin && std::all_of(covered.begin(), covered.end(), [](bool b) { return b; })) {
                reassembledGray.insert(sym);
                grayWhole.insert(sym);
            }
        }
        for (const auto& c : candidates) {
            if (reassembledGray.count(c.sym) && !c.whole && !c.inAsyncReset) continue;
            const bool isFlop = flopSymbolNode.count(c.sym) != 0;
            if (!isFlop && c.guarded) other.insert(c.sym);
            auto classify = [&](EvalContext& ec) {
                bool grayFunction = isGrayFunction(*c.rhs, ec);
                UserCallFinder calls;
                c.rhs->visit(calls);
                if (calls.found && !grayFunction) opaque.insert(c.sym);
                auto isAsyncResetSelfOrConst = [&](auto& self, const Expression& expr) -> bool {
                    if (!expr.eval(ec).bad()) return true;
                    const Expression& e = strip(expr);
                    if (e.kind == ExpressionKind::NamedValue) return e.getSymbolReference() == c.sym;
                    if (e.kind == ExpressionKind::ElementSelect) {
                        const auto& sel = e.as<ElementSelectExpression>();
                        return !sel.selector().eval(ec).bad() && self(self, sel.value());
                    }
                    if (e.kind == ExpressionKind::RangeSelect) {
                        const auto& sel = e.as<RangeSelectExpression>();
                        return !sel.left().eval(ec).bad() && !sel.right().eval(ec).bad() && self(self, sel.value());
                    }
                    if (e.kind == ExpressionKind::Concatenation) {
                        const auto& cat = e.as<ConcatenationExpression>();
                        return std::all_of(cat.operands().begin(), cat.operands().end(),
                                           [&](const Expression* op) { return op && self(self, *op); });
                    }
                    return false;
                };
                const bool isConst = !c.rhs->eval(ec).bad();
                const Expression& stripped = strip(*c.rhs);
                const bool isSelfHold = isFlop && c.whole &&
                    stripped.kind == ExpressionKind::NamedValue && stripped.getSymbolReference() == c.sym;
                const bool isAsyncResetHoldOrConst = isFlop && c.inAsyncReset &&
                    isAsyncResetSelfOrConst(isAsyncResetSelfOrConst, *c.rhs);
                if (!c.whole) {
                    if (!isAsyncResetHoldOrConst) other.insert(c.sym);
                    return;
                }
                if (isBinaryToGray(*c.rhs, ec, aliases) || grayFunction) grayWhole.insert(c.sym);
                else if (!isConst && !isSelfHold && !isAsyncResetHoldOrConst) other.insert(c.sym);
            };
            if (c.process) {
                EvalContext ec(*c.process);
                classify(ec);
            } else {
                ASTContext ac(comp.getRoot(), LookupLocation::max);
                EvalContext ec(ac);
                classify(ec);
            }
        }
        for (auto* sym : grayWhole) if (!other.count(sym) && !opaque.count(sym)) out.insert(sym);
    }
};

} // namespace

void Shared::findGrayNets() {
    if (!design_ || !design_->compilation) return;
    GrayFinder f(grayNets_, opaqueFunctionNets_, *design_->compilation, *m);
    design_->compilation->getRoot().visit(f);
    f.finish();
}

// The same source bits are asked about for every crossing group they launch: the answer is kept per node (opaqueFunctionNets_ is
// complete before the first question, see findGrayNets).
bool Shared::hasOpaqueFunction(NodeId q) const {
    if (opaqueFunctionOf_.size() != m->nodes.size()) opaqueFunctionOf_.assign(m->nodes.size(), -1);
    int8_t& known = opaqueFunctionOf_[q];
    if (known < 0) known = searchOpaqueFunction(q) ? 1 : 0;
    return known != 0;
}

bool Shared::searchOpaqueFunction(NodeId q) const {
    std::vector<NodeId> pending{q};
    std::unordered_set<NodeId> seen;
    while (!pending.empty()) {
        NodeId node = pending.back(); pending.pop_back();
        if (!seen.insert(node).second) continue;
        if (m->nodes[node].sym && opaqueFunctionNets_.count(m->nodes[node].sym)) return true;
        if (node != q && m->flopOfQ.count(node)) {
            // Follow an exact register-transfer alias as isGrayFlop does;
            // stop at independently computed state to avoid conflating it.
            NodeId source = kNoNode;
            bool alias = true;
            for (auto ei : m->in[node]) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Seq || e.ctrl) continue;
                if (!e.exact || source != kNoNode) { alias = false; break; }
                source = e.from;
            }
            if (alias && source != kNoNode) pending.push_back(source);
            continue;
        }
        for (auto ei : m->in[node]) {
            const Edge& e = m->edges[ei];
            if (!e.ctrl && (e.type == EdgeType::Comb || (node == q && e.type == EdgeType::Seq)))
                pending.push_back(e.from);
        }
    }
    return false;
}

bool Shared::isGrayFlop(NodeId q) const {
    NodeId cur = q;
    for (int guard = 0; guard < 16; guard++) {
        if (m->nodes[cur].sym && grayNets_.count(m->nodes[cur].sym)) return true;
        // follow a plain register-transfer / alias to its single source
        NodeId next = kNoNode;
        for (auto ei : m->in[cur]) {
            const Edge& e = m->edges[ei];
            if (e.type == EdgeType::AsyncCtl || e.ctrl) continue;
            if (!e.exact || next != kNoNode) return false;
            next = e.from;
        }
        if (next == kNoNode) return false;
        cur = next;
    }
    return false;
}

bool Shared::hasCtrlEdge(const Flop& d) const {
    auto isControlSource = [&](NodeId node) {
        NodeId cur = aliasRoot(node);
        if (syncedOut_.count(cur) || earlyQualifierTap_.count(cur)) return true;
        if (const auto* f = m->flopAt(cur))
            return f->clockResolved && d.clockResolved && relation(f->domain, d.domain) == Relation::Same;
        for (auto ei : m->in[cur]) {
            const Edge& e = m->edges[ei];
            if (e.type != EdgeType::Comb) continue;
            if (syncedOut_.count(e.from) || earlyQualifierTap_.count(e.from)) return true;
            if (const auto* f = m->flopAt(e.from))
                if (f->clockResolved && d.clockResolved && relation(f->domain, d.domain) == Relation::Same) return true;
        }
        return false;
    };
    // A mux with feedback is a hold-enable; arbitrary feedback (e.g. XOR)
    // is not necessarily an enable and must not be labelled one.
    if (hasFeedback(d)) {
        std::vector<NodeId> pending{d.q};
        std::unordered_set<NodeId> seen{d.q};
        while (!pending.empty()) {
            NodeId n = pending.back(); pending.pop_back();
            for (auto ei : m->in[n]) {
                const Edge& e = m->edges[ei];
                if (n == d.q ? e.type != EdgeType::Seq : e.type != EdgeType::Comb) continue;
                if (e.op == Op::MuxSel || (n != d.q && e.ctrl)) return true;
                if (e.ctrl) continue;
                if (!m->flopOfQ.count(e.from) && seen.insert(e.from).second) pending.push_back(e.from);
            }
        }
    }
    for (auto ei : m->in[d.q]) {
        const Edge& e = m->edges[ei];
        if (e.type != EdgeType::Seq) continue;
        if (e.ctrl) return true;
        if (e.op == Op::MuxSel && isControlSource(e.from)) return true;
        if (!e.ctrl && !m->flopOfQ.count(e.from)) {
            for (auto cei : m->in[e.from]) {
                const Edge& ce = m->edges[cei];
                if (ce.type == EdgeType::Comb && (ce.ctrl || ce.op == Op::MuxSel) && isControlSource(ce.from))
                    return true;
            }
            if (auto it = m->combQualifierDrivers.find(e.from); it != m->combQualifierDrivers.end()) {
                for (const auto& arm : it->second.arms) {
                    if (arm.controlDependencies)
                        for (NodeId c : *arm.controlDependencies)
                            if (isControlSource(c)) return true;
                }
            }
        }
    }
    return false;
}

// Sorts and deduplicates `v`, whose two halves [0, mid) and [mid, end) are usually sorted already: then in linear time.
static void sortedUnion(std::vector<NodeId>& v, std::ptrdiff_t mid) {
    if (std::is_sorted(v.begin(), v.begin() + mid) && std::is_sorted(v.begin() + mid, v.end()))
        std::inplace_merge(v.begin(), v.begin() + mid, v.end());
    else
        std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

bool Shared::qualifierLocalFlop(const Flop& launch, uint32_t captureDomain) const {
    if (!launch.clockResolved) return false;
    for (uint32_t dom : domainsOf(launch))
        if (relation(dom, captureDomain) != Relation::Same) return false;
    return true;
}

bool Shared::qualifierLocalInput(NodeId inNode, uint32_t captureDomain) const {
    if (rel_->hasMultipleClocks(captureDomain)) return false;
    bool rejectedMode = false;
    const auto contracts = rel_->effectiveInputDelays(inNode, &rejectedMode);
    if (rejectedMode || contracts.empty()) return false;
    return std::all_of(contracts.begin(), contracts.end(), [&](const auto& active) {
        return rel_->relationToName(captureDomain, active.delay->clock) == Relation::Same;
    });
}

bool Shared::qualifierSameSourceInput(NodeId inNode, uint32_t srcDomain, const std::string& sourceClock) const {
    bool rejectedMode = false;
    const auto contracts = rel_->effectiveInputDelays(inNode, &rejectedMode);
    if (rejectedMode || contracts.empty()) return false;
    return std::all_of(contracts.begin(), contracts.end(), [&](const auto& active) {
        if (!sourceClock.empty())
            return rel_->relationByNames(active.delay->clock, sourceClock) == Relation::Same;
        return !rel_->hasMultipleClocks(srcDomain) &&
               rel_->relationToName(srcDomain, active.delay->clock) == Relation::Same;
    });
}

bool Shared::qualifierSameSourceFlop(const Flop& launch, uint32_t srcDomain, const std::string& sourceClock) const {
    return launch.clockResolved && (!sourceClock.empty()
        ? rel_->relationToName(launch.domain, sourceClock) == Relation::Same
        : relation(launch.domain, srcDomain) == Relation::Same);
}

// The qualifier's alternate-data and same-source sets for many (capture, launch domain) pairs at once: the registers and top
// inputs each capture's cone walk reaches, sorted into the two sets by the predicates ctrlFromSynced applies to its view. A
// component is skipped where the view would skip it: every register behind it local to the capture and not on the launch
// domain, and no top input or uncertainty behind it (a skipped register belongs to neither set).
void Shared::precomputeQualifierSources(std::vector<std::pair<uint32_t, uint32_t>> keys) const {
    if (!qualifierClasses_)
        qualifierClasses_ = cones_->classify([&](uint32_t fi) {
            const Flop& f = m->flops[fi];
            return f.clockResolved && f.otherDomains.empty() && !m->multiSequential.count(f.q) ? f.domain : SourceClasses::kListed;
        }, true);
    const SourceClasses& cl = *qualifierClasses_;
    std::sort(keys.begin(), keys.end(), [&](const auto& a, const auto& b) {
        return std::tuple(m->flops[a.first].domain, a.second, a.first) < std::tuple(m->flops[b.first].domain, b.second, b.first);
    });
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    model::BatchWalker walker(*m);
    std::vector<NodeId> qs;
    std::vector<std::vector<NodeId>> alternate(model::BatchWalker::kLanes), sameSource(model::BatchWalker::kLanes);
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<const std::vector<NodeId>>>> interned;
    const auto intern = [&](std::vector<NodeId>& nodes) {
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        uint64_t hash = nodes.size();
        for (const NodeId node : nodes) hash = hash * 1099511628211u ^ node;
        auto& bucket = interned[hash];
        for (const auto& known : bucket) if (*known == nodes) return known;
        bucket.push_back(std::make_shared<const std::vector<NodeId>>(nodes));
        return bucket.back();
    };
    for (size_t first = 0; first < keys.size();) {
        const uint32_t captureDomain = m->flops[keys[first].first].domain, srcDomain = keys[first].second;
        size_t last = first;
        while (last < keys.size() && last - first < model::BatchWalker::kLanes && m->flops[keys[last].first].domain == captureDomain &&
               keys[last].second == srcDomain) ++last;
        qs.clear();
        for (size_t i = first; i < last; ++i) qs.push_back(m->flops[keys[i].first].q);
        std::unordered_map<uint32_t, bool> skipped;   // per class set: every class local to the capture and not the launch domain
        walker.run(qs, [&](NodeId n) {
            if (cl.open(n)) return false;
            const uint32_t set = cl.setOf(n);
            if (set == SourceClasses::kWide) return false;
            auto [it, fresh] = skipped.try_emplace(set, true);
            if (fresh)
                for (const uint32_t dom : cl.classes(set))
                    if (relation(dom, captureDomain) != Relation::Same || relation(dom, srcDomain) == Relation::Same) { it->second = false; break; }
            return it->second;
        });
        for (const NodeId leaf : walker.leaves()) {
            bool alt = false, same = false;
            if (const Flop* launch = m->flopAt(leaf)) {
                alt = !qualifierLocalFlop(*launch, captureDomain);
                same = qualifierSameSourceFlop(*launch, srcDomain, {});
            } else {   // a top input
                alt = !qualifierLocalInput(leaf, captureDomain);
                same = qualifierSameSourceInput(leaf, srcDomain, {});
            }
            if (!alt && !same) continue;
            const auto& lanes = walker.lanes(leaf);
            for (size_t w = 0; w < model::BatchWalker::kWords; ++w)
                for (uint64_t b = lanes[w]; b; b &= b - 1) {
                    const size_t lane = w * 64 + static_cast<size_t>(std::countr_zero(b));
                    if (qs[lane] == leaf) continue;   // a register is not a source of its own cone
                    if (alt) alternate[lane].push_back(leaf);
                    if (same) sameSource[lane].push_back(leaf);
                }
        }
        for (size_t lane = 0; lane < last - first; ++lane) {
            QualifierSources sources;
            sources.alternate = intern(alternate[lane]);
            sources.sameSource = intern(sameSource[lane]);
            precomputedSources_[{qs[lane], srcDomain}] = std::move(sources);
            alternate[lane].clear();
            sameSource[lane].clear();
        }
        first = last;
    }
}

Shared::QualifierResult Shared::ctrlFromSynced(const Flop& d, uint32_t srcDomain, const std::string& sourceClock,
                                               const std::set<NodeId>& sourceBits, std::vector<NodeId>* probed) const {
    // A probe asks as if for a source set that holds none of the nodes it tests, and lists those nodes.
    const bool hasSources = !sourceBits.empty() || probed;
    // A capture asked about many single source bits in one domain (reconvergence) gets its probe once: a set none of whose
    // bits the probe tested has the probe's answer. Elsewhere a probe costs more than the calls it saves.
    if (probeQualifiers_ && !probed && !sourceBits.empty()) {
        if (probeBytes_ > kQualifierCacheBytes) { probes_.clear(); probeBytes_ = 0; }
        auto [it, fresh] = probes_.try_emplace(QualifierKey{d.q, srcDomain, sourceClock});
        if (fresh) {
            std::vector<NodeId> tested;
            it->second.result = ctrlFromSynced(d, srcDomain, sourceClock, {}, &tested);
            std::sort(tested.begin(), tested.end());
            tested.shrink_to_fit();
            it->second.tested = std::move(tested);
            probeBytes_ += 160 + sourceClock.size() + it->second.tested.size() * sizeof(NodeId) + it->second.result.reason.size() +
                           it->second.result.necessaryQualifiers.size() * 48;
        }
        const auto& tested = it->second.tested;
        if (std::none_of(sourceBits.begin(), sourceBits.end(), [&](NodeId bit) { return std::binary_search(tested.begin(), tested.end(), bit); })) {
            ++probeHits_;
            return it->second.result;
        }
    }
    ++qualifierCalls_;
    const auto partStart = Timings::Clock::now();
    const auto nanosSince = [](Timings::Clock::time_point t) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Timings::Clock::now() - t).count());
    };
    auto git = m->qualifierGuards.find(d.q);
    if (git == m->qualifierGuards.end() || git->second.empty())
        return {Qualification::Disproven, "no guarded data-moving assignment"};
    if (expandedWritesBytes_ > kQualifierCacheBytes) { expandedWritesOf_.clear(); expandedWritesBytes_ = 0; ++qualifierExpansionClears_; }
    auto [expandedIt, expandFresh] = expandedWritesOf_.try_emplace(d.q);
    std::vector<ExpandedWrite>& expandedWrites = expandedIt->second.writes;
    bool& expansionLimited = expandedIt->second.limited;
    std::vector<ExpandedWrite> worklist;
    if (expandFresh) {
        ++qualifierExpansions_;
        worklist.reserve(git->second.size());
        for (const auto& w : git->second)
            worklist.push_back({w.guards, w.dataDependencies, w.controlDependencies, w.dataApproximate, w.controlApproximate, w.uncertainCall, false, false, 0, {}});
    }
    static constexpr size_t kMaxExpandedWrites = 64;
    static constexpr unsigned kMaxCombExpandDepth = 8;
    while (!worklist.empty()) {
        ExpandedWrite cur = std::move(worklist.back());
        worklist.pop_back();
        if (expandedWrites.size() + worklist.size() >= kMaxExpandedWrites || cur.depth >= kMaxCombExpandDepth) {
            expansionLimited = true;
            cur.expansionLimited = true;
            expandedWrites.push_back(std::move(cur));
            continue;
        }
        NodeId expandNode = kNoNode;
        if (cur.dataDependencies) {
            for (NodeId dep : *cur.dataDependencies) {
                if (m->flopAt(dep) || (dep < m->topInput.size() && m->topInput[dep]) ||
                    (dep < m->blackBoxDriven.size() && m->blackBoxDriven[dep]) ||
                    (dep < m->unresolvedDriven.size() && m->unresolvedDriven[dep]) ||
                    m->selfComb.count(dep) || cur.visitedComb.count(dep))
                    continue;
                if (m->combQualifierDrivers.find(dep) != m->combQualifierDrivers.end()) {
                    expandNode = dep;
                    break;
                }
            }
        }
        if (expandNode == kNoNode) {
            expandedWrites.push_back(std::move(cur));
            continue;
        }
        const auto& summary = m->combQualifierDrivers.at(expandNode);
        bool sameProcComb = summary.proc != UINT32_MAX;
        if (sameProcComb) {
            for (auto edgeId : m->in[expandNode]) {
                const auto& edge = m->edges[edgeId];
                if (edge.type == EdgeType::Comb && edge.proc != summary.proc) {
                    sameProcComb = false;
                    break;
                }
            }
        }
        if (!sameProcComb || !summary.supported || summary.multipleDrivers || summary.arms.empty()) {
            cur.incompleteComb |= !sameProcComb || summary.multipleDrivers || summary.incomplete;
            expandedWrites.push_back(std::move(cur));
            continue;
        }
        if (expandedWrites.size() + worklist.size() + summary.arms.size() > kMaxExpandedWrites) {
            expansionLimited = true;
            cur.expansionLimited = true;
            expandedWrites.push_back(std::move(cur));
            continue;
        }
        for (const auto& arm : summary.arms) {
            ExpandedWrite next;
            auto guards = std::make_shared<std::vector<GuardCondition>>(*cur.guards);
            guards->insert(guards->end(), arm.guards.begin(), arm.guards.end());
            next.guards = std::move(guards);
            auto mergedCtrl = std::make_shared<std::vector<NodeId>>();
            if (cur.controlDependencies) mergedCtrl->insert(mergedCtrl->end(), cur.controlDependencies->begin(), cur.controlDependencies->end());
            const auto ctrlMid = static_cast<std::ptrdiff_t>(mergedCtrl->size());
            if (arm.controlDependencies) mergedCtrl->insert(mergedCtrl->end(), arm.controlDependencies->begin(), arm.controlDependencies->end());
            sortedUnion(*mergedCtrl, ctrlMid);
            next.controlDependencies = std::move(mergedCtrl);
            if (cur.dataDependencies->size() == 1) {
                next.dataDependencies = arm.dataDependencies;
            } else {
                auto mergedData = std::make_shared<std::vector<NodeId>>();
                for (NodeId dep : *cur.dataDependencies)
                    if (dep != expandNode) mergedData->push_back(dep);
                const auto dataMid = static_cast<std::ptrdiff_t>(mergedData->size());
                if (arm.dataDependencies) mergedData->insert(mergedData->end(), arm.dataDependencies->begin(), arm.dataDependencies->end());
                sortedUnion(*mergedData, dataMid);
                next.dataDependencies = std::move(mergedData);
            }
            next.dataApproximate = cur.dataApproximate || arm.dataApproximate;
            next.controlApproximate = cur.controlApproximate || arm.controlApproximate;
            next.uncertainCall = cur.uncertainCall || arm.uncertainCall;
            next.incompleteComb = cur.incompleteComb || summary.incomplete || arm.unassigned;
            next.expansionLimited = cur.expansionLimited;
            next.depth = cur.depth + 1;
            next.visitedComb = cur.visitedComb;
            next.visitedComb.insert(expandNode);
            worklist.push_back(std::move(next));
        }
    }
    if (expandFresh) {
        for (auto& write : expandedWrites) {
            write.visitedComb = {};   // only the expansion reads it
            expandedWritesBytes_ += sizeof(ExpandedWrite);
            // An expanded write owns its guard and merged dependency lists; a write as written shares the model's.
            if (write.depth > 0) {
                expandedWritesBytes_ += write.guards->capacity() * sizeof(GuardCondition);
                for (const auto& guard : *write.guards) expandedWritesBytes_ += guard.dependencies.capacity() * sizeof(NodeId);
                for (const auto* deps : {write.dataDependencies.get(), write.controlDependencies.get()})
                    if (deps) expandedWritesBytes_ += deps->capacity() * sizeof(NodeId) + 48;
            }
        }
        expandedWritesBytes_ += 64;
    }
    bool uncertainSource = false;
    auto matching = [&](NodeId node) -> const SyncOut* {
        const SyncOut* match = nullptr;
        if (const auto it = syncedOut_.find(node); it != syncedOut_.end()) match = &it->second;
        else if (const auto tap = earlyQualifierTap_.find(node); tap != earlyQualifierTap_.end()) match = &tap->second;
        if (!match || match->dstDomain != d.domain || match->stageMismatch) return nullptr;
        if (match->sourceIncomplete) {
            uncertainSource = true;
            return match;
        }
        const auto& output = *match;
        if (rel_->hasMultipleClocks(d.domain)) return nullptr;
        if (output.sourceClock.empty() && sourceClock.empty())
            return output.srcDomain == srcDomain && !rel_->hasMultipleClocks(srcDomain) ? match : nullptr;
        if (output.sourceClock.empty())
            return rel_->relationToName(output.srcDomain, sourceClock) == Relation::Same ? match : nullptr;
        if (sourceClock.empty())
            return rel_->relationToName(srcDomain, output.sourceClock) == Relation::Same ? match : nullptr;
        return rel_->relationByNames(output.sourceClock, sourceClock) == Relation::Same ? match : nullptr;
    };
    QualifierResult overall{Qualification::ProvenNecessary, {}};
    std::set<std::pair<NodeId, bool>> commonNecessary;
    bool sawNecessaryWrite = false;
    bool sawLiveSourceWrite = false;
    bool anyIncompleteComb = false;
    if (qualifierSourcesBytes_ > kQualifierCacheBytes) {
        ++qualifierSourceClears_;
        qualifierSourcesOf_.clear();
        internedSourceSets_.clear();
        qualifierSourcesBytes_ = 0;
    }
    auto [sourcesIt, sourcesFresh] = qualifierSourcesOf_.try_emplace({d.q, srcDomain, sourceClock});
    std::vector<NodeId> alternateDataSources, sameSourceControls;   // filled on a first visit, then interned
    qualifierNanos_[0] += nanosSince(partStart);
    const auto sourcesStart = Timings::Clock::now();
    if (sourcesFresh) {   // computed on the first visit, whether or not this call has source bits
        ++qualifierSourceSets_;
        auto isLocalFlop = [&](const Flop& launch) { return qualifierLocalFlop(launch, d.domain); };
        auto isLocalInput = [&](NodeId inNode) { return qualifierLocalInput(inNode, d.domain); };
        auto isSameSourceInput = [&](NodeId inNode) { return qualifierSameSourceInput(inNode, srcDomain, sourceClock); };
        // Computed ahead for the whole group stage (precomputeQualifierSources): the same sets from a batched walk.
        if (const auto known = sourceClock.empty() ? precomputedSources_.find({d.q, srcDomain}) : precomputedSources_.end();
            known != precomputedSources_.end()) {
            sourcesIt->second = known->second;
            qualifierSourcesBytes_ += 96;
        } else {
        // Only sources in another domain, or on the launch clock, and the inputs are read: a register local to the capture and
        // not on the launch clock is neither an alternate data source nor a same-source control, so the cone is walked without it.
        if (!qualifierClasses_)
            qualifierClasses_ = cones_->classify([&](uint32_t fi) {
                const Flop& f = m->flops[fi];
                return f.clockResolved && f.otherDomains.empty() && !m->multiSequential.count(f.q) ? f.domain : SourceClasses::kListed;
            }, true);
        const auto [keyIt, freshKey] = qualifierKeys_.try_emplace({d.domain, srcDomain, sourceClock}, qualifierKeys_.size());
        const auto view = cones_->view(d, *qualifierClasses_, keyIt->second, [&](uint32_t set) {
            for (const uint32_t dom : qualifierClasses_->classes(set)) {
                if (relation(dom, d.domain) != Relation::Same) return false;
                const bool sameSource = !sourceClock.empty() ? rel_->relationToName(dom, sourceClock) == Relation::Same
                                                             : relation(dom, srcDomain) == Relation::Same;
                if (sameSource) return false;
            }
            return true;
        });
        const auto* cone = &view;
        // The group's own source bits are tested before these sets, so they are not left out here and the sets can be kept.
        for (const auto& source : cone->flops) {
            const auto* launch = m->flopAt(source.q);
            if (!launch) continue;
            if (!isLocalFlop(*launch)) alternateDataSources.push_back(source.q);
            if (qualifierSameSourceFlop(*launch, srcDomain, sourceClock))
                sameSourceControls.push_back(source.q);
        }
        for (const auto& input : cone->inputs) {
            if (!isLocalInput(input.q)) alternateDataSources.push_back(input.q);
            if (isSameSourceInput(input.q)) sameSourceControls.push_back(input.q);
        }
        for (auto* nodes : {&alternateDataSources, &sameSourceControls}) {
            std::sort(nodes->begin(), nodes->end());
            nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
        }
        sourcesIt->second.alternate = internSourceSet(std::move(alternateDataSources));
        sourcesIt->second.sameSource = internSourceSet(std::move(sameSourceControls));
        qualifierSourcesBytes_ += 96 + sourceClock.size();
        }
    }
    // The searches below ask about many nodes: the sources are marked once per call instead of looked up each time.
    if (controlSourceMark_.size() != m->nodes.size()) {
        controlSourceMark_.assign(m->nodes.size(), 0);
        sourceMarkGen_ = 0;
    }
    if (++sourceMarkGen_ == 0) {
        std::fill(controlSourceMark_.begin(), controlSourceMark_.end(), 0);
        sourceMarkGen_ = 1;
    }
    const uint32_t sourceMark = sourceMarkGen_;
    // The call's own source bits carry this call's mark; the cached sets keep theirs while the same set is asked about again.
    for (const NodeId node : sourceBits) controlSourceMark_[node] = sourceMark;
    const auto markSet = [&](const std::shared_ptr<const std::vector<NodeId>>& set, std::shared_ptr<const std::vector<NodeId>>& marked,
                             std::vector<uint32_t>& marks, uint32_t& generation) {
        if (marks.size() != m->nodes.size()) { marks.assign(m->nodes.size(), 0); generation = 0; marked.reset(); }
        if (marked == set) return;
        if (++generation == 0) { std::fill(marks.begin(), marks.end(), 0); generation = 1; }
        for (const NodeId node : *set) marks[node] = generation;
        marked = set;
    };
    markSet(sourcesIt->second.sameSource, markedControlSet_, controlSetMark_, controlSetGen_);
    markSet(sourcesIt->second.alternate, markedDataSet_, dataSetMark_, dataSetGen_);
    if (probed) {
        if (probeMark_.size() != m->nodes.size()) { probeMark_.assign(m->nodes.size(), 0); probeGen_ = 0; }
        if (++probeGen_ == 0) { std::fill(probeMark_.begin(), probeMark_.end(), 0); probeGen_ = 1; }
    }
    auto isSource = [&](NodeId node, bool control) {
        if (probed && probeMark_[node] != probeGen_) { probeMark_[node] = probeGen_; probed->push_back(node); }
        if (controlSourceMark_[node] == sourceMark) return true;
        return control ? controlSetMark_[node] == controlSetGen_ : dataSetMark_[node] == dataSetGen_;
    };
    struct SourcePaths { bool precise = false; bool approximate = false; };
    // The same dependency recurs across the expanded writes: each (start, role) is searched once per call (the call's source mark
    // stamps the memo, so a later call never reads it).
    for (auto& stamps : pathMemoStamp_) if (stamps.size() != m->nodes.size()) stamps.assign(m->nodes.size(), 0);
    for (auto& values : pathMemoValue_) if (values.size() != m->nodes.size()) values.assign(m->nodes.size(), 0);
    if (sourceMark == 1) for (auto& stamps : pathMemoStamp_) std::fill(stamps.begin(), stamps.end(), 0);   // the mark wrapped
    // The live bounded search from `start`; the reference the trace below must reproduce.
    auto liveSearchSourcePaths = [&](NodeId start, bool control, bool& limited) {
        if (isSource(start, control)) return SourcePaths{true, false};
        auto& pending = sourceSearchPending_;
        pending.clear();
        pending.emplace_back(start, false);
        // (node, approximate) pairs seen in this search, as stamps: the same order and the same 256-pair limit as a set.
        const uint32_t generation = nextFeedbackGeneration();
        size_t visitedCount = 0;
        SourcePaths paths;
        while (!pending.empty()) {
            const auto [node, pathApproximate] = pending.back(); pending.pop_back();
            auto& stamp = pathApproximate ? foreStamp_ : backStamp_;
            if (stamp[node] == generation) continue;
            stamp[node] = generation;
            ++visitedCount;
            if (isSource(node, control)) {
                if (pathApproximate) paths.approximate = true;
                else paths.precise = true;
                continue;
            }
            if (visitedCount > 256) { limited = true; break; }
            for (auto edgeId : m->in[node]) {
                const auto& edge = m->edges[edgeId];
                // Source identity follows same-cycle connectivity only; crossing a
                // register D edge would turn stale state or a self-hold into a new write.
                if (edge.type != EdgeType::Comb) continue;
                // A precise control dependency can pass through Boolean
                // logic. Exact bit transfer is required only for data provenance.
                pending.emplace_back(edge.from, pathApproximate || edge.approximate || (!control && !edge.exact));
            }
        }
        return paths;
    };
    // Every source is a register output or a top-level input. When none the search reaches has a Comb in-edge (checked while the
    // trace is built), expanding one adds nothing, so the sequence of new states it pops is the same for every source set: only which leaves count as sources, and where the
    // search stops past the 256th state, depend on the set. The trace records the leaves of the first 256 states and the states
    // past the 256th up to the first non-leaf (a non-leaf is never a source, so every call stops there at the latest). A call
    // then replays it: sources among the leaves set the flags; past the 256th state a source sets its flag and the first
    // non-source sets `limited` and stops, exactly as the live loop does. Control and data differ in approximation, so they
    // are separate traces.
    constexpr size_t kPostBound = 64;
    auto isLeaf = [&](NodeId node) { return cones_->isRegister(node) || (node < m->topInput.size() && m->topInput[node]); };
    auto searchSourcePaths = [&](NodeId start, bool control, bool& limited) {
        if (isSource(start, control)) return SourcePaths{true, false};
        const uint64_t key = (static_cast<uint64_t>(start) << 1) | (control ? 1 : 0);
        auto found = sourceTraces_.find(key);
        if (found == sourceTraces_.end()) {
            SourceTrace trace;
            auto& pending = sourceSearchPending_;
            pending.clear();
            pending.emplace_back(start, false);
            const uint32_t generation = nextFeedbackGeneration();
            size_t visitedCount = 0;
            while (!pending.empty()) {
                const auto [node, pathApproximate] = pending.back(); pending.pop_back();
                auto& stamp = pathApproximate ? foreStamp_ : backStamp_;
                if (stamp[node] == generation) continue;
                stamp[node] = generation;
                ++visitedCount;
                const bool leaf = isLeaf(node);
                // A possible source with a combinational driver (a multiply driven register, an inout) is expanded by a query
                // that does not hold it: then the order depends on the source set, and every call searches live.
                if (leaf && std::any_of(m->in[node].begin(), m->in[node].end(), [&](uint32_t e) { return m->edges[e].type == EdgeType::Comb; })) {
                    trace.overflow = true;
                    break;
                }
                if (visitedCount > 256) {
                    if (trace.post.size() >= kPostBound) { trace.overflow = true; break; }
                    trace.post.push_back({node, pathApproximate, leaf});
                    if (!leaf) break;
                    continue;
                }
                if (leaf) { trace.leaves.emplace_back(node, pathApproximate); continue; }
                for (auto edgeId : m->in[node]) {
                    const auto& edge = m->edges[edgeId];
                    if (edge.type != EdgeType::Comb) continue;
                    pending.emplace_back(edge.from, pathApproximate || edge.approximate || (!control && !edge.exact));
                }
            }
            const size_t bytes = 96 + trace.leaves.capacity() * sizeof(trace.leaves[0]) + trace.post.capacity() * sizeof(trace.post[0]);
            if (sourceTraceBytes_ + bytes > kSourceTraceBytes) { sourceTraces_.clear(); sourceTraceBytes_ = 0; }
            sourceTraceBytes_ += bytes;
            found = sourceTraces_.emplace(key, std::move(trace)).first;
        }
        const SourceTrace& trace = found->second;
        if (trace.overflow) return liveSearchSourcePaths(start, control, limited);
        SourcePaths paths;
        for (const auto& [node, approximate] : trace.leaves)
            if (isSource(node, control)) (approximate ? paths.approximate : paths.precise) = true;
        for (const auto& state : trace.post) {
            if (state.leaf && isSource(state.node, control)) { (state.approximate ? paths.approximate : paths.precise) = true; continue; }
            limited = true;
            break;
        }
        return paths;
    };
    auto sourcePaths = [&](NodeId start, bool control, bool& limited) {
        auto& stamp = pathMemoStamp_[control][start];
        auto& value = pathMemoValue_[control][start];
        if (stamp != sourceMark) {
            bool hit = false;
            const SourcePaths found = searchSourcePaths(start, control, hit);
            stamp = sourceMark;
            value = static_cast<uint8_t>((found.precise ? 1 : 0) | (found.approximate ? 2 : 0) | (hit ? 4 : 0));
        }
        limited |= (value & 4) != 0;
        return SourcePaths{(value & 1) != 0, (value & 2) != 0};
    };
    auto unresolvedNextState = [&](NodeId start) {
        std::vector<NodeId> pending{start};
        std::unordered_set<NodeId> visited;
        bool feedback = false, synchronizedSelect = false;
        while (!pending.empty()) {
            const NodeId node = pending.back();
            pending.pop_back();
            if (node == d.q) { feedback = true; continue; }
            if (m->flopAt(node) || !visited.insert(node).second) continue;
            if (visited.size() > 64) return false;
            for (const auto edgeId : m->in[node]) {
                const auto& edge = m->edges[edgeId];
                if (edge.type != EdgeType::Comb) continue;
                if (edge.ctrl || edge.op == Op::MuxSel)
                    synchronizedSelect |= matching(aliasRoot(edge.from)) != nullptr;
                else
                    pending.push_back(edge.from);
            }
        }
        return feedback && synchronizedSelect;
    };
    qualifierNanos_[1] += nanosSince(sourcesStart);
    struct WritesTimer {
        uint64_t& total; Timings::Clock::time_point start;
        ~WritesTimer() { total += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Timings::Clock::now() - start).count()); }
    } writesTimer{qualifierNanos_[2], Timings::Clock::now()};
    for (const auto& write : expandedWrites) {
        if (hasSources) {
            bool limited = false;
            SourcePaths controlPaths;
            if (write.controlDependencies) {
                for (auto dependency : *write.controlDependencies) {
                    const auto paths = sourcePaths(dependency, true, limited);
                    controlPaths.precise |= paths.precise;
                    controlPaths.approximate |= paths.approximate;
                }
            }
            if (controlPaths.precise && !write.controlApproximate)
                return {Qualification::Disproven, "the crossing source is sampled as capture control, not as qualified payload data"};
            if (controlPaths.precise || controlPaths.approximate) {
                overall = {Qualification::Undecided, "an approximate crossing-source dependency controls the capture"};
                sawLiveSourceWrite = true;
                continue;
            }
            bool carriesSource = false;
            bool approximateDependency = false;
            if (write.dataDependencies) {
                for (auto dependency : *write.dataDependencies) {
                    const auto paths = sourcePaths(dependency, false, limited);
                    carriesSource |= paths.precise || paths.approximate;
                    approximateDependency |= paths.approximate;
                }
            }
            if (limited) {
                overall = {Qualification::Undecided, "the source-specific dependency search reached its limit"};
                if (!carriesSource) continue;
            }
            if (!carriesSource) {
                if (write.uncertainCall) {
                    overall = {Qualification::Undecided, "an opaque call prevents proving whether this write carries crossing data"};
                    sawLiveSourceWrite = true;
                }
                continue;
            }
            if (write.dataApproximate || approximateDependency) {
                sawLiveSourceWrite = true;
                if (write.guards->empty()) {
                    if (write.incompleteComb || write.expansionLimited ||
                        (approximateDependency && write.dataDependencies &&
                         std::any_of(write.dataDependencies->begin(), write.dataDependencies->end(), unresolvedNextState))) {
                        overall = {Qualification::Undecided, write.incompleteComb
                            ? "combinational next-state logic has an incomplete or multi-driver path"
                            : write.expansionLimited
                            ? "next-state combinational expansion reached its limit"
                            : "next-state feedback passes through a synchronized selector with unresolved data arms"};
                        continue;
                    }
                    return {Qualification::Disproven, "an unguarded source-dependent write bypasses synchronized control"};
                }
                overall = {Qualification::Undecided, "the source-specific data dependency is approximate"};
                continue;
            }
        } else if (write.uncertainCall) {
            overall = {Qualification::Undecided, "an opaque call prevents proving capture-control necessity"};
            continue;
        }
        const auto& assignment = *write.guards;
        uncertainSource = false;
        bool uncertainEdge = false;
        if (assignment.empty()) {
            if (write.incompleteComb || write.expansionLimited) {
                sawLiveSourceWrite = true;
                overall = {Qualification::Undecided, write.incompleteComb
                    ? "combinational next-state logic has an incomplete or multi-driver path"
                    : "next-state combinational expansion reached its limit"};
                continue;
            }
            return {Qualification::Disproven, "an unguarded data-moving assignment bypasses synchronized control"};
        }
        // Expand only supported Boolean syntax. Registers are Boolean atoms,
        // not combinational substitutes for the state transitions driving them.
        struct Formula {
            BoolFact::Kind kind;
            bool value = false;
            size_t atom = 0;
            std::vector<std::shared_ptr<Formula>> args;
        };
        using Expr = std::shared_ptr<Formula>;
        auto make = [](BoolFact::Kind kind, std::vector<Expr> args = {}) {
            auto f = std::make_shared<Formula>(); f->kind = kind; f->args = std::move(args); return f;
        };
        size_t atomCount = 0, facts = 0;
        bool limited = false, unknown = false, opaque = false, reachableMatching = false, foreignControl = false;
        const SyncOut* shortQualifier = nullptr;   // a qualifying synchroniser below the required depth
        std::unordered_set<NodeId> reached, active;
        std::unordered_map<NodeId, Expr> atoms, cache;
        std::set<size_t> synchronizedAtoms, stateAtoms;
        auto atom = [&](NodeId node) {
            if (node != kNoNode) {
                auto it = atoms.find(node);
                if (it != atoms.end()) return it->second;
            }
            auto f = make(BoolFact::Kind::Node); f->atom = atomCount++;
            if (node != kNoNode) atoms[node] = f;
            return f;
        };
        auto reachable = [&](NodeId start) {
            std::vector<NodeId> pending{start};
            std::unordered_set<NodeId> visited;
            bool found = false;
            while (!pending.empty()) {
                NodeId node = pending.back(); pending.pop_back();
                if (!visited.insert(node).second) continue;
                reached.insert(node);
                if (reached.size() > 64) { limited = true; return found; }
                if (matching(node)) { reachableMatching = found = true; continue; }
                for (auto ei : m->in[node]) {
                    const auto& e = m->edges[ei];
                    if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) pending.push_back(e.from);
                }
            }
            return found;
        };
        std::function<Expr(NodeId)> expandNode;
        std::function<Expr(const BoolFactPtr&)> expand;
        expand = [&](const BoolFactPtr& fact) -> Expr {
            if (++facts > 256) { limited = true; return atom(kNoNode); }
            if (!fact) { unknown = true; return atom(kNoNode); }
            if (fact->kind == BoolFact::Kind::Node) return expandNode(fact->node);
            if (fact->kind == BoolFact::Kind::Unknown) {
                unknown = true; opaque = opaque || fact->opaque;
                for (const auto& dependency : fact->args)
                    if (dependency->kind == BoolFact::Kind::Node) reachable(dependency->node);
                return atom(kNoNode);
            }
            auto f = make(fact->kind); f->value = fact->value;
            for (const auto& arg : fact->args) f->args.push_back(expand(arg));
            return f;
        };
        expandNode = [&](NodeId node) -> Expr {
            if (const auto* output = matching(node)) {
                reachableMatching = true;
                uncertainEdge |= output->dualEdgeStage;
                if (!output->full) shortQualifier = output;
                auto f = atom(node); synchronizedAtoms.insert(f->atom); return f;
            }
            if (auto it = cache.find(node); it != cache.end()) return it->second;
            bool stateDependency = reachable(node);
            if (!active.insert(node).second) { unknown = true; return atom(node); }
            Expr result;
            if (const auto* control = m->flopAt(node)) {
                const bool sameControlDomain = control->clockResolved && relation(control->domain, d.domain) == Relation::Same;
                if (!sameControlDomain) foreignControl = true;
                result = atom(node);
                if (stateDependency && sameControlDomain) stateAtoms.insert(result->atom);
            } else if (auto it = m->booleanDrivers.find(node); it != m->booleanDrivers.end()) {
                if (it->second.size() == 1 && it->second[0].unconditional && it->second[0].value)
                    result = expand(it->second[0].value);
                else { unknown = true; result = atom(node); }
            } else {
                const Edge* alias = nullptr;
                bool other = false;
                for (auto ei : m->in[node]) {
                    const auto& e = m->edges[ei];
                    if (e.type != EdgeType::Comb || e.ctrl) { other = true; continue; }
                    if (alias || (!e.exact && !(e.op == Op::Not && e.inv == 1))) other = true;
                    alias = &e;
                }
                if (alias && !other) {
                    result = expandNode(alias->from);
                    if (alias->inv == 1) result = make(BoolFact::Kind::Not, {result});
                } else {
                    if (alias) unknown = true;
                    result = atom(node);
                }
            }
            active.erase(node); cache[node] = result; return result;
        };
        std::vector<Expr> guards;
        for (const auto& guard : assignment) guards.push_back(expand(guard.predicate));
        auto predicate = make(BoolFact::Kind::And, std::move(guards));
        auto undecided = [&](const std::string& reason) { overall = {Qualification::Undecided, reason}; };
        if (limited || atomCount > 12) { sawLiveSourceWrite = true; undecided("qualifier walk or Boolean proof limit was reached"); continue; }
        if (opaque) { sawLiveSourceWrite = true; undecided("an opaque call prevents proving capture-control necessity"); continue; }
        if (foreignControl) return {Qualification::Disproven, "the capture guard contains a control not synchronized into the capture domain"};
        std::function<bool(const Expr&, size_t)> evaluate = [&](const Expr& f, size_t values) {
            switch (f->kind) {
                case BoolFact::Kind::Constant: return f->value;
                case BoolFact::Kind::Node: return ((values >> f->atom) & 1) != 0;
                case BoolFact::Kind::Not: return !evaluate(f->args[0], values);
                case BoolFact::Kind::Equal: return evaluate(f->args[0], values) == evaluate(f->args[1], values);
                case BoolFact::Kind::And:
                    for (const auto& a : f->args) if (!evaluate(a, values)) return false;
                    return true;
                case BoolFact::Kind::Or:
                    for (const auto& a : f->args) if (evaluate(a, values)) return true;
                    return false;
                default: return false; // Unknown was replaced by an unconstrained atom.
            }
        };
        std::map<size_t, unsigned> possible;
        for (auto id : synchronizedAtoms) possible[id] = 0;
        for (size_t values = 0; values < (size_t(1) << atomCount); ++values) {
            if (!evaluate(predicate, values)) continue;
            for (auto id : synchronizedAtoms) possible[id] |= 1u << ((values >> id) & 1);
        }
        sawLiveSourceWrite = true;
        anyIncompleteComb |= write.incompleteComb;
        if (synchronizedAtoms.empty()) {
            if (reachableMatching) undecided("matching synchronized control is reachable only through state or unsupported Boolean logic");
            else if (write.incompleteComb || write.expansionLimited)
                undecided(write.incompleteComb
                    ? "combinational next-state logic has an incomplete or multi-driver path"
                    : "next-state combinational expansion reached its limit");
            else return {Qualification::Disproven, "no matching source-to-destination synchronized control reaches the guard"};
            continue;
        }
        std::set<std::pair<NodeId, bool>> necessaryCandidates;
        for (auto [id, values] : possible) {
            if (values == 1 || values == 2) {
                for (const auto& [node, expression] : atoms)
                    if (expression->atom == id)
                        necessaryCandidates.emplace(node, values == 2);
            }
        }
        const bool necessary = !necessaryCandidates.empty();
        if (necessary) {   // Holds even with unsupported subexpressions treated as arbitrary values.
            const auto previousNecessary = commonNecessary;
            if (!sawNecessaryWrite) commonNecessary = necessaryCandidates;
            else {
                std::set<std::pair<NodeId, bool>> shared;
                std::set_intersection(commonNecessary.begin(), commonNecessary.end(),
                                      necessaryCandidates.begin(), necessaryCandidates.end(),
                                      std::inserter(shared, shared.end()));
                commonNecessary = std::move(shared);
            }
            sawNecessaryWrite = true;
            if (commonNecessary.empty()) {
                if (necessaryCandidates.size() == 1 && previousNecessary.size() == 1 &&
                    necessaryCandidates.begin()->first == previousNecessary.begin()->first &&
                    necessaryCandidates.begin()->second != previousNecessary.begin()->second)
                    return {Qualification::Disproven, "complementary data-moving writes require opposite polarities of the synchronized control"};
                undecided("distinct synchronized controls are necessary on different data-moving writes");
                continue;
            }
            // A qualifier short of the required depth may itself be metastable when it opens the capture (CDC-005 reports it).
            if (uncertainSource)
                undecided("the recognized qualifier synchronizer has no single established launch source");
            else if (uncertainEdge)
                undecided("the qualifying synchronizer has a required dual-edge stage with unresolved timing");
            else if (shortQualifier)
                undecided("the qualifying synchroniser of " + shortQualifier->srcName + " has " + std::to_string(shortQualifier->depth) +
                          " stage(s), below the required " + std::to_string(minStages_));
            continue;
        }
        // State-derived validity can be held after the synchronized input changes.
        // If it can widen the predicate through OR, Boolean syntax alone does
        // not establish whether that branch is a legitimate held token.
        bool heldValidity = false;
        std::function<void(const Expr&, bool, bool)> findHeld = [&](const Expr& f, bool negated, bool disjunction) {
            if (f->kind == BoolFact::Kind::Node) {
                if (disjunction && stateAtoms.count(f->atom)) heldValidity = true;
                return;
            }
            if (f->kind == BoolFact::Kind::Not) { findHeld(f->args[0], !negated, disjunction); return; }
            bool isOr = f->kind == BoolFact::Kind::Equal ||
                        (f->kind == BoolFact::Kind::Or && !negated) || (f->kind == BoolFact::Kind::And && negated);
            for (const auto& arg : f->args) findHeld(arg, negated, disjunction || isOr);
        };
        findHeld(predicate, false, false);
        if (unknown || heldValidity || synchronizedAtoms.size() > 1 || write.incompleteComb || write.expansionLimited)
            undecided(write.incompleteComb
                ? "combinational next-state logic has an incomplete or multi-driver path"
                : write.expansionLimited
                ? "next-state combinational expansion reached its limit"
                : "Boolean or state semantics do not establish a necessary synchronized control");
        else return {Qualification::Disproven, "capture has a Boolean bypass of the synchronized control"};
    }
    if (hasSources && !sawLiveSourceWrite)
        return {Qualification::Undecided, "the model cannot associate a data-moving write precisely with this crossing source"};
    if (overall.state == Qualification::ProvenNecessary && expansionLimited)
        return {Qualification::Undecided, "next-state combinational expansion reached its limit"};
    if (overall.state == Qualification::ProvenNecessary && anyIncompleteComb)
        return {Qualification::Undecided, "combinational next-state logic has an incomplete or multi-driver path"};
    if (overall.state == Qualification::ProvenNecessary && (!sawNecessaryWrite || commonNecessary.empty()))
        return {Qualification::Disproven, "no single synchronized qualifier with one polarity is necessary across every source-specific data-moving write"};
    if (overall.state == Qualification::ProvenNecessary && !commonNecessary.empty()) {
        const auto [node, asserted] = *commonNecessary.begin();
        overall.reason = "synchronized qualifier " + m->nameOf(node) + " must be " +
                         (asserted ? "asserted" : "deasserted") + " on every source-specific data-moving write";
        overall.necessaryQualifiers = commonNecessary;
    }
    return overall;
}

bool Shared::counterLike(const std::set<NodeId>& srcBits) {
    for (auto q : srcBits) {
        auto it = m->flopOfQ.find(q);
        if (it == m->flopOfQ.end()) continue;
        const auto heldCone = coneOf(m->flops[it->second]);
        const Cone& c = *heldCone;
        for (auto& s : c.flops)
            if (s.q != q && srcBits.count(s.q)) return true;   // the next value of one bit depends on another bit of the bus: carry chain
    }
    return false;
}

bool Shared::hasLiveBinaryCounterUpdate(const std::set<NodeId>& srcBits) {
    if (srcBits.size() < 2) return false;
    const NodeId head = *srcBits.begin();
    if (head >= m->nodes.size() || !m->nodes[head].sym) return false;
    const auto* targetInst = m->nodes[head].inst;
    const auto* targetSym = m->nodes[head].sym;
    for (NodeId q : srcBits) {
        if (q >= m->nodes.size() || m->nodes[q].inst != targetInst || m->nodes[q].sym != targetSym) return false;
    }
    struct SymRefFinder : ASTVisitor<SymRefFinder, VisitFlags::AllGood> {
        const ValueSymbol* target = nullptr;
        bool found = false;
        void handle(const NamedValueExpression& e) {
            if (&e.symbol == target) found = true;
        }
    };
    auto referencesSym = [&](const Expression& e, const ValueSymbol* sym) -> bool {
        SymRefFinder f;
        f.target = sym;
        e.visit(f);
        return f.found;
    };
    auto isConstZero = [&](const Expression& e, EvalContext& ec) -> bool {
        auto v = e.eval(ec);
        if (v.bad() || !v.isInteger()) return false;
        return !v.integer().hasUnknown() && v.integer() == 0;
    };
    auto simpleGuardSym = [&](const ConditionalStatement& br) -> const ValueSymbol* {
        if (br.conditions.size() != 1 || br.conditions[0].pattern || !br.conditions[0].expr) return nullptr;
        const Expression& e = strip(*br.conditions[0].expr);
        if (e.kind == ExpressionKind::NamedValue) return e.getSymbolReference()->as_if<ValueSymbol>();
        return nullptr;
    };
    std::function<bool(const ProceduralBlockSymbol*, const ValueSymbol*, int)> procHasLiveArith;
    std::function<bool(const Expression&, EvalContext&, int)> isBinaryArithUpdate =
        [&](const Expression& raw, EvalContext& ec, int depth) -> bool {
        if (depth > 4) return false;
        const Expression& e = strip(raw);
        if (e.kind == ExpressionKind::ConditionalOp) {
            const auto& cond = e.as<ConditionalExpression>();
            if (cond.conditions.size() == 1 && !cond.conditions[0].pattern && cond.conditions[0].expr) {
                if (auto val = model::elaboratedCondition(*cond.conditions[0].expr, ec))
                    return *val ? isBinaryArithUpdate(cond.left(), ec, depth + 1)
                                : isBinaryArithUpdate(cond.right(), ec, depth + 1);
            }
            return isBinaryArithUpdate(cond.left(), ec, depth + 1) || isBinaryArithUpdate(cond.right(), ec, depth + 1);
        }
        if (e.kind == ExpressionKind::BinaryOp) {
            const auto& bin = e.as<BinaryExpression>();
            if (bin.op == BinaryOperator::Add || bin.op == BinaryOperator::Subtract) {
                const bool leftHas = referencesSym(bin.left(), targetSym);
                const bool rightHas = referencesSym(bin.right(), targetSym);
                if (leftHas && !isConstZero(bin.right(), ec)) return true;
                if (rightHas && bin.op == BinaryOperator::Add && !isConstZero(bin.left(), ec)) return true;
            }
        }
        if (e.kind == ExpressionKind::NamedValue) {
            const auto* ref = e.getSymbolReference();
            const auto* aliasSym = ref ? ref->as_if<ValueSymbol>() : nullptr;
            if (aliasSym && aliasSym != targetSym) {
                std::set<const ProceduralBlockSymbol*> aliasProcs;
                for (const auto& w : m->proceduralWrites) {
                    if (w.instance == targetInst && w.target == aliasSym && w.process) aliasProcs.insert(w.process);
                }
                for (const auto* ap : aliasProcs) {
                    if (procHasLiveArith(ap, aliasSym, depth + 1)) return true;
                }
                if (const auto* net = aliasSym->as_if<NetSymbol>(); net && net->getInitializer()) {
                    if (isBinaryArithUpdate(*net->getInitializer(), ec, depth + 1)) return true;
                }
                if (targetInst) {
                    for (const auto& ca : targetInst->body.membersOfType<ContinuousAssignSymbol>()) {
                        const auto& expr = ca.getAssignment();
                        if (expr.kind == ExpressionKind::Assignment) {
                            const auto& asg = expr.as<AssignmentExpression>();
                            if (asg.left().getSymbolReference() == aliasSym) {
                                if (isBinaryArithUpdate(asg.right(), ec, depth + 1)) return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    };

    procHasLiveArith = [&](const ProceduralBlockSymbol* proc, const ValueSymbol* lhsSym, int depth) -> bool {
        EvalContext ec(*proc);
        struct Outcome {
            bool liveArith = false;
            bool wholeOverwrite = false;
            const ValueSymbol* soleGuard = nullptr;
            bool writesGuard = false;
        };
        std::function<Outcome(const Statement&)> evalStmt = [&](const Statement& stmt) -> Outcome {
            switch (stmt.kind) {
                case StatementKind::List: {
                    struct GuardWriteFinder : ASTVisitor<GuardWriteFinder, VisitFlags::AllGood> {
                        const ValueSymbol* target = nullptr;
                        bool written = false;
                        void handle(const AssignmentExpression& a) {
                            if (a.left().getSymbolReference() == target) written = true;
                            visitDefault(a);
                        }
                    };
                    Outcome acc{};
                    for (const auto* s : stmt.as<StatementList>().list) {
                        if (!s) continue;
                        if (acc.liveArith && acc.soleGuard && s->kind == StatementKind::Conditional) {
                            const auto& br = s->as<ConditionalStatement>();
                            if (simpleGuardSym(br) == acc.soleGuard) {
                                Outcome t = evalStmt(br.ifTrue);
                                Outcome f = br.ifFalse ? evalStmt(*br.ifFalse) : Outcome{};
                                if (t.wholeOverwrite && !t.liveArith && !f.liveArith) {
                                    acc.liveArith = false;
                                    acc.soleGuard = nullptr;
                                    continue;
                                }
                            }
                        }
                        if (acc.soleGuard) {
                            GuardWriteFinder gwf;
                            gwf.target = acc.soleGuard;
                            s->visit(gwf);
                            if (gwf.written) acc.soleGuard = nullptr;
                        }
                        Outcome step = evalStmt(*s);
                        if (step.wholeOverwrite) {
                            acc.liveArith = step.liveArith;
                            acc.soleGuard = step.soleGuard;
                        } else if (step.liveArith) {
                            if (!acc.liveArith) acc.soleGuard = step.soleGuard;
                            else if (acc.soleGuard != step.soleGuard) acc.soleGuard = nullptr;
                            acc.liveArith = true;
                        }
                        acc.wholeOverwrite = acc.wholeOverwrite || step.wholeOverwrite;
                    }
                    return acc;
                }
                case StatementKind::Block:
                    return evalStmt(stmt.as<BlockStatement>().body);
                case StatementKind::Timed:
                    return evalStmt(stmt.as<TimedStatement>().stmt);
                case StatementKind::Conditional: {
                    const auto& br = stmt.as<ConditionalStatement>();
                    if (br.conditions.size() == 1 && !br.conditions[0].pattern && br.conditions[0].expr) {
                        if (auto val = model::elaboratedCondition(*br.conditions[0].expr, ec)) {
                            if (*val) return evalStmt(br.ifTrue);
                            return br.ifFalse ? evalStmt(*br.ifFalse) : Outcome{};
                        }
                    }
                    Outcome t = evalStmt(br.ifTrue);
                    Outcome f = br.ifFalse ? evalStmt(*br.ifFalse) : Outcome{};
                    const ValueSymbol* guard = (t.liveArith && !f.liveArith) ? simpleGuardSym(br) : nullptr;
                    return {t.liveArith || f.liveArith, t.wholeOverwrite && f.wholeOverwrite, guard, false};
                }
                case StatementKind::ExpressionStatement: {
                    const auto& expr = stmt.as<ExpressionStatement>().expr;
                    if (expr.kind != ExpressionKind::Assignment) return {};
                    const auto& asg = expr.as<AssignmentExpression>();
                    const auto* ref = asg.left().getSymbolReference();
                    if (ref != lhsSym) return {};
                    const bool whole = asg.left().kind == ExpressionKind::NamedValue;
                    const bool arith = isBinaryArithUpdate(asg.right(), ec, depth);
                    return {arith, whole, nullptr, false};
                }
                case StatementKind::Case: {
                    const auto& cs = stmt.as<CaseStatement>();
                    Outcome acc{};
                    for (const auto& item : cs.items) {
                        if (item.stmt) acc.liveArith |= evalStmt(*item.stmt).liveArith;
                    }
                    if (cs.defaultCase) acc.liveArith |= evalStmt(*cs.defaultCase).liveArith;
                    return acc;
                }
                case StatementKind::ForLoop:
                    return {evalStmt(stmt.as<ForLoopStatement>().body).liveArith, false, nullptr, false};
                case StatementKind::RepeatLoop:
                    return {evalStmt(stmt.as<RepeatLoopStatement>().body).liveArith, false, nullptr, false};
                case StatementKind::ForeachLoop:
                    return {evalStmt(stmt.as<ForeachLoopStatement>().body).liveArith, false, nullptr, false};
                case StatementKind::WhileLoop:
                    return {evalStmt(stmt.as<WhileLoopStatement>().body).liveArith, false, nullptr, false};
                case StatementKind::DoWhileLoop:
                    return {evalStmt(stmt.as<DoWhileLoopStatement>().body).liveArith, false, nullptr, false};
                default:
                    return {};
            }
        };
        return evalStmt(proc->getBody()).liveArith;
    };

    // Collect distinct sequential processes that write targetSym and evaluate live (non-dead, non-overwritten) writes.
    std::set<const ProceduralBlockSymbol*> procs;
    for (const auto& w : m->proceduralWrites) {
        if (w.instance == targetInst && w.target == targetSym && w.process) procs.insert(w.process);
    }
    for (const auto* proc : procs) {
        if (procHasLiveArith(proc, targetSym, 0)) return true;
    }
    return false;
}

// CDC-018 may use timing coverage as one part of its trust model only after
// the binary state feeding the recognized Gray conversion has a directly
// visible unit-step transition. Keep this deliberately narrow: syntax shape
// and cross-bit dependence alone also match LFSRs and arbitrary feedback.
bool Shared::grayUnitStep(const std::set<NodeId>& grayBits) {
    constexpr size_t kMaxSourceBits = 4096;
    constexpr size_t kMaxWrites = 100'000;
    if (m->proceduralWrites.size() > kMaxWrites) return false;
    if (grayBits.empty() || grayBits.size() > kMaxSourceBits) return false;
    const NodeId grayHead = *grayBits.begin();
    const auto* grayFlop = m->flopAt(grayHead);
    const auto* graySymbol = m->nodes[grayHead].sym;
    if (!grayFlop || !graySymbol || !grayNets_.contains(graySymbol) ||
        graySymbol->getType().getBitstreamWidth() != grayBits.size() || grayFlop->edge == 3) return false;
    std::set<NodeId> binaryBits;
    for (NodeId q : grayBits) {
        auto fit = m->flopOfQ.find(q);
        if (fit == m->flopOfQ.end()) return false;
        const Flop& flop = m->flops[fit->second];
        if (m->nodes[q].sym != graySymbol || flop.proc != grayFlop->proc || !flop.clockResolved ||
            flop.domain != grayFlop->domain || flop.edge != grayFlop->edge || !flop.otherDomains.empty() ||
            flop.blocking || flop.hasIff || flop.syncReset != kNoNode || !flop.syncResetFacts.empty() ||
            hasCtrlEdge(flop)) return false;
        const auto cone = coneOf(flop);
        if (cone->flops.size() > kMaxSourceBits || !cone->inputs.empty() || !cone->bbLeaves.empty() ||
            !cone->unresolved.empty() || !cone->uncertainCalls.empty() || !cone->undriven.empty()) return false;
        for (const auto& source : cone->flops) {
            binaryBits.insert(source.q);
            if (binaryBits.size() > kMaxSourceBits) return false;
        }
    }
    if (binaryBits.empty()) return false;

    const slang::ast::ValueSymbol* symbol = nullptr;
    std::set<uint32_t> bitIndices;
    std::optional<uint32_t> process;
    uint32_t domain = 0;
    for (NodeId q : binaryBits) {
        if (q >= m->nodes.size() || !m->nodes[q].sym) return false;
        const auto* current = m->nodes[q].sym;
        if (symbol && symbol != current) return false;
        symbol = current;
        if (!bitIndices.insert(m->nodes[q].bit).second) return false;
        auto fit = m->flopOfQ.find(q);
        if (fit == m->flopOfQ.end()) return false;
        const Flop& flop = m->flops[fit->second];
        if (!flop.clockResolved || !flop.otherDomains.empty() || flop.domain != grayFlop->domain ||
            flop.edge != grayFlop->edge || flop.blocking || flop.hasIff) return false;
        if (!process) { process = flop.proc; domain = flop.domain; }
        else if (*process != flop.proc || domain != flop.domain) return false;
    }
    const uint64_t width = symbol->getType().getBitstreamWidth();
    if (width < 2 || width != grayBits.size() || width != binaryBits.size() || bitIndices.size() != width) return false;
    for (uint32_t bit = 0; bit < width; ++bit)
        if (!bitIndices.count(bit)) return false;

    const auto resetSignature = asyncResetSignature(*grayFlop);
    if (resetSignature.size() > 1) return false;
    std::vector<char> binaryReset(width, '0');
    auto resetBit = [&](NodeId q) -> char {
        const auto& f = *m->flopAt(q);
        if (asyncResetSignature(f) != resetSignature || f.resets.size() != resetSignature.size() ||
            f.syncReset != kNoNode || !f.syncResetFacts.empty()) return '?';
        if (resetSignature.empty()) return '0';
        return f.resetValues.size() == 1 ? f.resetValues.front() : '?';
    };
    for (NodeId q : binaryBits) {
        const char value = resetBit(q);
        if (value != '0' && value != '1') return false;
        binaryReset[m->nodes[q].bit] = value;
    }
    for (NodeId q : grayBits) {
        const auto bit = m->nodes[q].bit;
        if (bit >= width) return false;
        const char next = bit + 1 < width ? binaryReset[bit + 1] : '0';
        if (resetBit(q) != (binaryReset[bit] == next ? '0' : '1')) return false;
    }

    auto trackedSymbol = [&](const auto* target) { return target == symbol || target == graySymbol; };
    for (const auto& write : m->proceduralWrites)
        if (std::any_of(write.mayWrite.begin(), write.mayWrite.end(), trackedSymbol)) return false;
    for (const auto& write : m->systemWrites) if (trackedSymbol(write.target)) return false;
    for (const auto& write : m->copyBackWrites) if (trackedSymbol(write.target)) return false;

    auto isDirectBinaryToGray = [&](const slang::ast::Expression& rhs, slang::ast::EvalContext& ec) -> bool {
        const auto* e = stripNonNarrowing(rhs, width);
        if (!e) return false;
        if (e->kind == slang::ast::ExpressionKind::Call) {
            if (!isGrayFunction(*e, ec)) return false;
            const auto& call = e->as<slang::ast::CallExpression>();
            return call.arguments().size() == 1 && isUnnarrowedWholeReferenceTo(*call.arguments()[0], symbol, width);
        }
        if (e->kind != slang::ast::ExpressionKind::BinaryOp) return false;
        const auto& b = e->as<slang::ast::BinaryExpression>();
        if (b.op != slang::ast::BinaryOperator::BinaryXor) return false;
        auto shiftedByOneSelf = [&](const slang::ast::Expression& operand) -> bool {
            const auto* s = stripNonNarrowing(operand, width);
            if (!s || s->kind != slang::ast::ExpressionKind::BinaryOp) return false;
            const auto& sh = s->as<slang::ast::BinaryExpression>();
            if (sh.op != slang::ast::BinaryOperator::LogicalShiftRight &&
                !(sh.op == slang::ast::BinaryOperator::ArithmeticShiftRight && sh.left().type && !sh.left().type->isSigned()))
                return false;
            auto amount = sh.right().eval(ec);
            if (!amount.isInteger() || amount.integer().hasUnknown()) return false;
            auto val = amount.integer().as<int64_t>();
            return val && *val == 1 && isExactWidthWholeReferenceTo(sh.left(), symbol, width);
        };
        return (isUnnarrowedWholeReferenceTo(b.left(), symbol, width) && shiftedByOneSelf(b.right())) ||
               (isUnnarrowedWholeReferenceTo(b.right(), symbol, width) && shiftedByOneSelf(b.left()));
    };

    const NodeId binHead = *binaryBits.begin();
    bool sawUnitStep = false;
    unsigned samples = 0;
    unsigned grayResetWrites = 0;
    for (const auto& write : m->proceduralWrites) {
        if (write.target == graySymbol) {
            if (!write.value || !write.process || !write.nonBlocking || !write.exactTarget || write.partialTarget ||
                write.called || write.fromCall || write.afterPossibleExit || write.copyBackValueUnknown ||
                write.forkOwnership || write.throughCopyFormal) return false;
            if (write.constantValue) {
                if (resetSignature.empty() || !write.wholeConstant || write.scopes.size() != 2 ||
                    !matchesResetArm(*m, grayHead, write.scopes[0], write.scopes[1], true) ||
                    ++grayResetWrites != 1)
                    return false;
                slang::ast::EvalContext ec(*write.process);
                auto value = write.value->eval(ec);
                if (!value.isInteger() || value.integer().hasUnknown()) return false;
                for (NodeId q : grayBits) {
                    const uint32_t bit = m->nodes[q].bit;
                    if (bit >= width || bit >= value.integer().getBitWidth()) return false;
                    const char next = bit + 1 < width ? binaryReset[bit + 1] : '0';
                    const char expected = binaryReset[bit] == next ? '0' : '1';
                    const char stored = value.integer()[static_cast<int32_t>(bit)] == slang::logic_t(true) ? '1' :
                                        value.integer()[static_cast<int32_t>(bit)] == slang::logic_t(false) ? '0' : '?';
                    if (stored != expected) return false;
                }
            } else {
                slang::ast::EvalContext ec(*write.process);
                const bool inNormalArm = resetSignature.empty()
                    ? write.scopes.empty()
                    : (write.scopes.size() == 2 && matchesResetArm(*m, grayHead, write.scopes[0], write.scopes[1], false));
                if (!inNormalArm || !isDirectBinaryToGray(*write.value, ec) || ++samples != 1)
                    return false;
            }
            continue;
        }
        if (write.target != symbol) continue;
        if (!write.value || write.called || write.fromCall || write.copyBackValueUnknown ||
            write.forkOwnership || write.throughCopyFormal || write.partialTarget || !write.exactTarget ||
            !write.nonBlocking || write.afterPossibleExit)
            return false;
        if (write.constantValue) {
            // Constant whole-vector writes are accepted only for the model's
            // exact asynchronous reset transition. Other constant updates can
            // jump between Gray states and are not unit-step evidence.
            if (resetSignature.empty() || !write.wholeConstant || write.scopes.size() != 2 ||
                !matchesResetArm(*m, binHead, write.scopes[0], write.scopes[1], true))
                return false;
            if (!write.process) return false;
            slang::ast::EvalContext context(*write.process);
            auto value = write.value->eval(context);
            if (!value.isInteger() || value.integer().hasUnknown()) return false;
            const auto reset = synchronousReset(*write.process, *m);
            if (!reset || reset->known.empty()) return false;
            bool resetWrite = false;
            for (const auto& branch : reset->known) {
                if (branch.compound || !storesConstantWhileAsserted(write, branch) ||
                    reachWhileAsserted(write, branch) != Reach::Runs) continue;
                bool matches = true;
                for (NodeId q : binaryBits) {
                    const Flop& flop = m->flops[m->flopOfQ.at(q)];
                    const uint32_t bit = m->nodes[q].bit;
                    if (flop.resets.size() != 1 || flop.resetValues.size() != 1 ||
                        (flop.resetValues[0] != '0' && flop.resetValues[0] != '1') ||
                        flop.resets[0].signal >= m->nodes.size() ||
                        m->nodes[flop.resets[0].signal].sym != branch.signal ||
                        branch.assertedHigh == flop.resets[0].activeLow || bit >= value.integer().getBitWidth()) {
                        matches = false;
                        break;
                    }
                    const char stored = value.integer()[static_cast<int32_t>(bit)] == slang::logic_t(true) ? '1' :
                                        value.integer()[static_cast<int32_t>(bit)] == slang::logic_t(false) ? '0' : '?';
                    if (stored != flop.resetValues[0]) { matches = false; break; }
                }
                if (matches) { resetWrite = true; break; }
            }
            if (!resetWrite) return false;
            continue;
        }
        if (!write.process) return false;
        if (!resetSignature.empty() &&
            (write.scopes.size() < 2 || !matchesResetArm(*m, binHead, write.scopes[0], write.scopes[1], false)))
            return false;
        if (isUnnarrowedWholeReferenceTo(*write.value, symbol, width)) continue;
        const auto* expr = stripNonNarrowing(*write.value, width);
        if (!expr || expr->kind != slang::ast::ExpressionKind::BinaryOp) return false;
        const auto& op = expr->as<slang::ast::BinaryExpression>();
        const bool plus = op.op == slang::ast::BinaryOperator::Add;
        const bool minus = op.op == slang::ast::BinaryOperator::Subtract;
        if (!plus && !minus) return false;
        const auto isSelf = [&](const slang::ast::Expression& value) {
            return isUnnarrowedWholeReferenceTo(value, symbol, width);
        };
        const auto isOne = [&](const slang::ast::Expression& value) {
            slang::ast::EvalContext context(*write.process);
            auto evaluated = value.eval(context);
            if (!evaluated.isInteger() || evaluated.integer().hasUnknown()) return false;
            auto integer = evaluated.integer().as<int64_t>();
            return integer && *integer == 1;
        };
        const bool unit = (isSelf(op.left()) && isOne(op.right())) ||
                          (plus && isOne(op.left()) && isSelf(op.right()));
        if (!unit) return false;
        sawUnitStep = true;
    }
    return sawUnitStep && samples == 1 && (resetSignature.empty() || grayResetWrites == 1);
}

std::vector<const sdc::PathException*> Shared::exceptionsFor(NodeId src, NodeId dst, bool anyKind,
                                                              sdc::PathException::Kind kind) const {
    std::vector<const sdc::PathException*> out;
    auto matches = [&](const std::vector<sdc::ObjectRef>& refs, NodeId n) {
        const std::string rtlBit = rel_->rtlBitName(n);
        const std::string full = stripBit(m->nameOf(n));
        for (auto& r : refs) {
            if (r.regexp || r.unsupportedOption) continue; // retained and disclosed; never reinterpret as a glob
            if (r.kind == "get_clocks" || r.kind == "all_clocks") {      // clock objects: match the SDC clock of the register's domain
                const Flop* f = m->flopAt(n);
                if (r.kind == "all_clocks" || (f && f->clockResolved && globMatch(r.pattern, domainClockName(f->domain)))) return true;
                continue;
            }
            if (r.kind.empty()) {
                const Flop* f = m->flopAt(n);
                if (f && f->clockResolved && !domainClockName(f->domain).empty() &&
                    globMatch(r.pattern, domainClockName(f->domain))) return true;
            }
            if (r.kind == "get_cells") {
                std::string p = r.pattern;
                std::replace(p.begin(), p.end(), '/', '.');
                while (!p.empty() && p.back() == '.') p.pop_back();
                if (!p.empty()) {
                    for (const auto& instPath : m->subInstancePaths) {
                        auto dot = instPath.find('.');
                        std::string rel = dot == std::string::npos ? instPath : instPath.substr(dot + 1);
                        if ((globMatch(p, instPath) || globMatch(p, rel)) &&
                            (full == instPath || (full.size() > instPath.size() &&
                                                  full.compare(0, instPath.size(), instPath) == 0 &&
                                                  full[instPath.size()] == '.')))
                            return true;
                    }
                }
                if (m->flopOfQ.count(n) &&
                    ((!rtlBit.empty() && rel_->nameMatches(r.kind, r.pattern, n, rtlBit)) ||
                     rel_->nameMatches(r.kind, r.pattern, n, full)))
                    return true;
                continue;
            }
            if ((!rtlBit.empty() && rel_->nameMatches(r.kind, r.pattern, n, rtlBit)) ||
                rel_->nameMatches(r.kind, r.pattern, n, full)) return true;
        }
        return false;
    };
    for (auto& ex : cfg->constraints.exceptions) {
        if (!anyKind && ex.kind != kind) continue;
        if (ex.from.empty() && ex.to.empty()) {
            if (!ex.through.empty() && (matches(ex.through, src) || matches(ex.through, dst))) out.push_back(&ex);   // -through: matched by name only
            continue;
        }
        // -through is not path-evaluated: an exception with -through is reported as possibly covering the crossing
        if ((ex.from.empty() || matches(ex.from, src)) && (ex.to.empty() || matches(ex.to, dst))) out.push_back(&ex);
    }
    return out;
}

} // namespace sverity::cdc

// ======================================================================================================
// Structure analyses over the crossing groups (reconvergence, clocks built from logic, async controls,
// toggle / handshake / FIFO protocols, memories, attributes). Every classification is structural: a
// recognised protocol is reported INFO together with its proof obligation, never as proven safe.
// ======================================================================================================
#include "slang/ast/symbols/InstanceSymbols.h"

namespace sverity::cdc {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

double Shared::periodNs(uint32_t domain, bool* unitKnown) const {
    if (unitKnown) *unitKnown = false;
    const std::string& n = domainClockName(domain);
    if (n.empty()) return 0;
    for (auto it = cfg->constraints.clocks.rbegin(); it != cfg->constraints.clocks.rend(); ++it)
        if ((it->name.empty() ? (it->sources.empty() ? std::string() : it->sources[0].pattern) : it->name) == n) {
            if (unitKnown) *unitKnown = it->periodTimeUnitKnown;
            return it->periodNs;
        }
    return 0;
}

void Shared::analyseStructure() {
    for (const char* r : {"CDC-010", "CDC-019", "CDC-020", "CDC-030", "CDC-031", "CDC-043", "CST-013",
                          "CDC-012", "CDC-013", "CDC-014", "CDC-015", "CDC-024", "CDC-034", "CDC-035", "CDC-037", "CDC-038",
                          "CDC-040", "CDC-041", "CDC-044", "CDC-023", "CDC-045", "CDC-046", "CDC-047", "RDC-023", "RDC-024"})
        evaluated[r] += 0;
    sequentialElements();
    const auto reconvergenceStart = Timings::Clock::now();
    reconvergence();
    if (cfg->timings) cfg->timings->record("cdc:reconvergence", reconvergenceStart);
    clockStructure();
    edgeStructure();
    libraryCells();
    protocols();
}

// --- CDC-001: level-sensitive elements have no clock; never silently skipped ------------------------------------
void Shared::sequentialElements() {
    for (size_t pi = 0; pi < m->procs.size(); pi++) {
        if (m->procs[pi].kind != ProcKind::Latch) continue;
        evaluated["CDC-001"]++;
        Diagnostic d = mk("CDC-001", Severity::Warning, "level-sensitive element (latch) has no clock edge: it is not assigned to a clock domain",
                          m->procs[pi].loc, m->procs[pi].instancePath, Confidence::Unresolved);
        d.nextStep = "Review how data crosses this latch; CDC analysis treats its output as an unclassified source.";
        add(std::move(d));
        incomplete("clock-extraction", "latch at " + m->procs[pi].loc.file + ":" + std::to_string(m->procs[pi].loc.line));
    }
}

// --- CDC-010 / 011 / 036: several asynchronous influences meeting in one destination cone -----------------------
void Shared::reconvergence() {
    precomputedSources_ = {};   // the group stage's sets: this pass asks about its own captures
    // Names are built once per node: registers read thousands of the same sources (fullName: nameOf; symbolId: stripBit of it).
    // Influences are keyed by name id (equal names share one id, as equal string keys did) and sorted by name only when reported.
    std::vector<uint32_t> fullNameOf(m->nodes.size(), UINT32_MAX), symbolIdOf(m->nodes.size(), UINT32_MAX);
    std::vector<std::string> fullNames, symbolNames;
    std::unordered_map<std::string, uint32_t> fullNameIds, symbolIds;
    const auto fullNameId = [&](NodeId n) {
        if (fullNameOf[n] == UINT32_MAX) {
            auto [it, fresh] = fullNameIds.try_emplace(m->nameOf(n), static_cast<uint32_t>(fullNames.size()));
            if (fresh) fullNames.push_back(it->first);
            fullNameOf[n] = it->second;
        }
        return fullNameOf[n];
    };
    const auto symbolId = [&](NodeId n) {
        if (symbolIdOf[n] == UINT32_MAX) {
            auto [it, fresh] = symbolIds.try_emplace(stripBit(fullNames[fullNameId(n)]), static_cast<uint32_t>(symbolNames.size()));
            if (fresh) symbolNames.push_back(it->first);
            symbolIdOf[n] = it->second;
        }
        return symbolIdOf[n];
    };
    // synchronised signals that reach a destination-domain register directly: followed one register stage further, so that
    // reconvergence behind a pipeline register is also found
    std::map<NodeId, std::map<uint32_t, const SyncOut*>> derived;
    std::vector<const SyncOut*> syncedOfFlop(m->flops.size(), nullptr);   // syncedOut_ by register index
    for (const auto& [q, so] : syncedOut_)
        if (const uint32_t fi = cones_->flopIndexOf(q); fi != UINT32_MAX) syncedOfFlop[fi] = &so;
    // Both loops read only synchroniser outputs and sources with another relation: the cones are walked without the rest.
    const auto synchronised = cones_->classify([&](uint32_t fi) {
        const Flop& f = m->flops[fi];
        return f.clockResolved && syncedOut_.count(f.q) ? SourceClasses::kListed : SourceClasses::kIgnore;
    }, false);
    for (uint32_t di = 0; di < m->flops.size(); di++) {
        const Flop& d = m->flops[di];
        if (!d.clockResolved) continue;
        const ConeView c = cones_->view(d, synchronised, 0, [](uint32_t) { return true; });
        for (auto& s : c.flops) {
            const uint32_t si = cones_->flopIndexOf(s.q);
            const Flop& sf = m->flops[si];
            if (!sf.clockResolved || relation(sf.domain, d.domain) != Relation::Same) continue;
            if (const SyncOut* so = syncedOfFlop[si]) derived[d.q][fullNameId(so->srcQ)] = so;
        }
    }
    // Every bit of a pipeline register usually carries the same influences: equal maps share one interned sorted list, so a
    // destination merges each distinct list once.
    using Influences = std::vector<std::pair<uint32_t, const SyncOut*>>;
    std::vector<Influences> derivedLists;
    std::vector<uint32_t> derivedListOf(m->flops.size(), UINT32_MAX);   // by register index
    {
        std::map<Influences, uint32_t> interned;
        for (const auto& [q, influences] : derived) {
            Influences list(influences.begin(), influences.end());
            auto [it, fresh] = interned.try_emplace(std::move(list), static_cast<uint32_t>(derivedLists.size()));
            if (fresh) derivedLists.push_back(it->first);
            derivedListOf[cones_->flopIndexOf(q)] = it->second;
        }
    }
    std::vector<uint32_t> lastUseOf(derivedLists.size(), UINT32_MAX);   // per destination: the last contribution reading a list
    std::vector<uint32_t> syncedSlotStamp, syncedSlot;                    // per name id: stamp of the destination, index in synced
    uint32_t syncedStamp = 0;
    struct GroupKey {
        uint32_t proc = 0;
        std::string kind;
        bool unresolvedSource = false;
        bool hasUnresolvedQual = false;
        std::string list;
        bool operator<(const GroupKey& o) const {
            return std::tie(proc, kind, unresolvedSource, hasUnresolvedQual, list) <
                   std::tie(o.proc, o.kind, o.unresolvedSource, o.hasUnresolvedQual, o.list);
        }
    };
    struct GroupEntry {
        std::vector<std::string> dsts;
        size_t influenceCount = 0;
        SourceLocation loc;
        std::string hier;
    };
    std::map<GroupKey, GroupEntry> grouped;
    // Loop 2 needs, per destination d of domain D, the clock-resolved registers its cone walk reaches (from d's Seq inputs back
    // through every node that is not a named register or top input, along Comb and Seq edges; d itself excluded): a
    // same-domain synchroniser output or derived register contributes, a register of any other relation is a raw candidate,
    // and nothing else is read. Up to kLanes destinations of one domain are walked together, one bit lane each. The walk's
    // graph is condensed once; a batch visits the components it reaches in order, each once and only after every component
    // that leads to it, so a cone shared by the batch is walked once. A node is not entered when every register behind it is
    // same-domain and neither a synchroniser output nor derived (domain masks): such registers contribute nothing.
    const NodeId nodeCount = static_cast<NodeId>(m->nodes.size());
    std::vector<uint8_t> stops(nodeCount, 0);   // the walk lists or ignores these and never goes behind them
    for (NodeId n = 0; n < nodeCount; ++n)
        stops[n] = m->nodes[n].sym && (cones_->isRegister(n) || (n < m->topInput.size() && m->topInput[n]));
    const auto follow = [&](const Edge& e) { return (e.type == EdgeType::Comb || e.type == EdgeType::Seq) && !stops[e.to]; };
    const Condensation walkGraph = condense(*m, follow);
    const auto special = [&](uint32_t fi) { return syncedOfFlop[fi] || derivedListOf[fi] != UINT32_MAX; };
    const bool masked = m->domains.size() <= 64;
    std::vector<uint64_t> regMask, specMask, sameMask;   // per node: domains reachable (all, special); per domain: Same relations
    if (masked) {
        regMask.assign(nodeCount, 0);
        specMask.assign(nodeCount, 0);
        for (uint32_t fi = 0; fi < m->flops.size(); ++fi) {
            const Flop& f = m->flops[fi];
            if (!f.clockResolved || !m->nodes[f.q].sym) continue;
            regMask[f.q] = uint64_t{1} << f.domain;
            if (special(fi)) specMask[f.q] = regMask[f.q];
        }
        closeMasks(*m, walkGraph, regMask, specMask, follow);
        sameMask.assign(m->domains.size(), 0);
        for (uint32_t a = 0; a < m->domains.size(); ++a)
            for (uint32_t b = 0; b < m->domains.size(); ++b)
                if (relation(b, a) == Relation::Same) sameMask[a] |= uint64_t{1} << b;
    }
    std::vector<uint32_t> visitOrder;
    for (uint32_t di = 0; di < m->flops.size(); di++)
        if (m->flops[di].clockResolved) visitOrder.push_back(di);
    std::stable_sort(visitOrder.begin(), visitOrder.end(),
                     [&](uint32_t a, uint32_t b) { return m->flops[a].domain < m->flops[b].domain; });
    struct Candidate {
        uint32_t di = 0;
        std::string dst;
        GroupKey key;
        size_t influenceCount = 0;
    };
    // Only the first-wins choice per dst + kind and the first destination of a group depend on the visiting order: both are
    // replayed in register order after the loop.
    std::map<std::string, Candidate> candidates;   // per dst + kind: the candidate with the lowest register index
    constexpr size_t kWords = 4, kLanes = kWords * 64;
    using Lanes = std::array<uint64_t, kWords>;
    const auto hasLane = [](const Lanes& l, size_t lane) { return (l[lane >> 6] >> (lane & 63)) & 1; };
    const auto setLane = [](Lanes& l, size_t lane) { l[lane >> 6] |= uint64_t{1} << (lane & 63); };
    const auto forLanes = [](const Lanes& l, auto&& f) {
        for (size_t w = 0; w < kWords; ++w)
            for (uint64_t b = l[w]; b; b &= b - 1) f(w * 64 + static_cast<size_t>(std::countr_zero(b)));
    };
    uint64_t batches = 0, components = 0, edgeScans = 0, leafVisits = 0, visitedDestinations = 0;
    const auto loopStart = Timings::Clock::now();
    std::vector<Lanes> acc(nodeCount, Lanes{});   // the lanes whose walk reached a node
    std::vector<uint64_t> pendingComponents((walkGraph.componentEnd.size() + 63) / 64, 0);
    std::vector<NodeId> touched, leaves;
    std::vector<uint16_t> laneOfFlop(m->flops.size(), 0);   // 1 + lane of a destination in the current batch
    std::vector<Lanes> rawLanes, unresolvedLanes;            // per symbol id, for the current batch
    std::vector<uint32_t> symbolBatch;                       // per symbol id: the batch that last touched it
    std::vector<uint32_t> touchedSymbols;
    std::vector<Lanes> rawDomainLanes(m->domains.size());
    struct Lane {
        std::vector<std::tuple<NodeId, uint32_t, const SyncOut*>> contributions;   // (register, name id, output) or (register, list id, nullptr)
        std::vector<const Influences*> alreadyReconvergedInputs;
        std::vector<uint32_t> rawIds;   // symbols with a bit that is not proven qualified
        size_t unresolvedCount = 0, rawDomainCount = 0;
        uint32_t firstRawDomain = 0;
    };
    std::vector<Lane> lanes(kLanes);
    probeQualifiers_ = true;
    for (size_t first = 0; first < visitOrder.size();) {
        const uint32_t domain = m->flops[visitOrder[first]].domain;
        size_t last = first;
        while (last < visitOrder.size() && last - first < kLanes && m->flops[visitOrder[last]].domain == domain) ++last;
        const size_t width = last - first;
        ++batches;
        const uint64_t same = masked ? sameMask[domain] : 0;
        const auto pruned = [&](NodeId n) { return masked && (regMask[n] & ~same) == 0 && (specMask[n] & same) == 0; };
        const auto reach = [&](NodeId n, const Lanes& bits) {
            if (pruned(n)) return;
            Lanes& a = acc[n];
            uint64_t fresh = 0, before = 0;
            for (size_t w = 0; w < kWords; ++w) { fresh |= bits[w] & ~a[w]; before |= a[w]; }
            if (!fresh) return;
            if (!before) { touched.push_back(n); if (stops[n]) leaves.push_back(n); }
            for (size_t w = 0; w < kWords; ++w) a[w] |= bits[w];
            if (!stops[n]) { const uint32_t c = walkGraph.componentOf[n]; pendingComponents[c >> 6] |= uint64_t{1} << (c & 63); }
        };
        Lanes guarded{};   // destinations with guarded writes: only their raw candidates need a qualification call
        for (size_t lane = 0; lane < width; ++lane) {
            const Flop& d = m->flops[visitOrder[first + lane]];
            laneOfFlop[visitOrder[first + lane]] = static_cast<uint16_t>(lane + 1);
            if (auto git = m->qualifierGuards.find(d.q); git != m->qualifierGuards.end() && !git->second.empty()) setLane(guarded, lane);
            Lanes bit{};
            setLane(bit, lane);
            for (auto ei : m->in[d.q])
                if (m->edges[ei].type == EdgeType::Seq) reach(m->edges[ei].from, bit);
        }
        // Edges between components run from higher numbers to lower ones, and the walk follows them backwards: ascending
        // order completes a component's lanes before it passes them on.
        for (size_t w = 0; w < pendingComponents.size(); ++w)
            while (pendingComponents[w]) {
                const uint32_t c = static_cast<uint32_t>(w * 64 + std::countr_zero(pendingComponents[w]));
                pendingComponents[w] &= pendingComponents[w] - 1;
                ++components;
                const uint32_t begin = c ? walkGraph.componentEnd[c - 1] : 0, end = walkGraph.componentEnd[c];
                Lanes all{};
                for (uint32_t i = begin; i < end; ++i)
                    for (size_t k = 0; k < kWords; ++k) all[k] |= acc[walkGraph.order[i]][k];
                if (end - begin > 1)   // every node of a cycle reaches every other
                    for (uint32_t i = begin; i < end; ++i) {
                        const NodeId v = walkGraph.order[i];
                        bool before = false;
                        for (size_t k = 0; k < kWords; ++k) before |= acc[v][k] != 0;
                        if (!before) touched.push_back(v);
                        acc[v] = all;
                    }
                for (uint32_t i = begin; i < end; ++i)
                    for (auto ei : m->in[walkGraph.order[i]]) {
                        ++edgeScans;
                        const Edge& e = m->edges[ei];
                        if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) reach(e.from, all);
                    }
            }
        const uint32_t batchStamp = static_cast<uint32_t>(batches);
        // Raw candidates are qualified per (destination, bit); the sets they fill do not depend on the order of the calls.
        for (const NodeId q : leaves) {
            const uint32_t si = cones_->flopIndexOf(q);
            if (si == UINT32_MAX) continue;   // a top input
            const Flop& sf = m->flops[si];
            if (!sf.clockResolved) continue;
            Lanes bits = acc[q];
            if (laneOfFlop[si]) bits[(laneOfFlop[si] - 1) >> 6] &= ~(uint64_t{1} << ((laneOfFlop[si] - 1) & 63));   // not its own source
            ++leafVisits;
            if (relation(sf.domain, domain) == Relation::Same) {
                const SyncOut* so = syncedOfFlop[si];
                const uint32_t list = derivedListOf[si];
                if (!so && list == UINT32_MAX) continue;
                const uint32_t id = so ? fullNameId(so->srcQ) : list;
                forLanes(bits, [&](size_t lane) {
                    Lane& l = lanes[lane];
                    l.contributions.emplace_back(q, id, so);
                    if (!so && derivedLists[list].size() >= 2) l.alreadyReconvergedInputs.push_back(&derivedLists[list]);
                });
                continue;
            }
            uint64_t any = 0;
            for (size_t k = 0; k < kWords; ++k) any |= bits[k];
            if (!any) continue;
            const uint32_t id = symbolId(q);
            if (id >= rawLanes.size()) { rawLanes.resize(id + 1, Lanes{}); unresolvedLanes.resize(id + 1, Lanes{}); symbolBatch.resize(id + 1, 0); }
            if (symbolBatch[id] != batchStamp) { symbolBatch[id] = batchStamp; touchedSymbols.push_back(id); }
            Lanes& rawOf = rawLanes[id];
            Lanes& unresolvedOf = unresolvedLanes[id];
            Lanes& domainLanes = rawDomainLanes[sf.domain];
            // Without guarded writes the qualification is Disproven: raw, not unresolved, no call needed.
            for (size_t k = 0; k < kWords; ++k) { rawOf[k] |= bits[k] & ~guarded[k]; domainLanes[k] |= bits[k] & ~guarded[k]; }
            Lanes asked{};
            for (size_t k = 0; k < kWords; ++k) asked[k] = bits[k] & guarded[k];
            forLanes(asked, [&](size_t lane) {
                // Whatever this bit's qualification, it can add nothing once its symbol is already raw, unresolved and of this domain.
                if (hasLane(rawOf, lane) && hasLane(unresolvedOf, lane) && hasLane(domainLanes, lane)) return;
                const Qualification state = ctrlFromSynced(m->flops[visitOrder[first + lane]], sf.domain, {}, {q}).state;
                if (state == Qualification::ProvenNecessary) return;
                setLane(rawOf, lane);
                setLane(domainLanes, lane);
                if (state == Qualification::Undecided) setLane(unresolvedOf, lane);
            });
        }
        for (const uint32_t id : touchedSymbols) {
            forLanes(rawLanes[id], [&](size_t lane) { lanes[lane].rawIds.push_back(id); });
            forLanes(unresolvedLanes[id], [&](size_t lane) { lanes[lane].unresolvedCount++; });
        }
        for (uint32_t x = 0; x < rawDomainLanes.size(); ++x)
            forLanes(rawDomainLanes[x], [&](size_t lane) {
                Lane& l = lanes[lane];
                if (!l.rawDomainCount++) l.firstRawDomain = x;
            });
        for (const NodeId n : touched) acc[n] = Lanes{};
        touched.clear();
        leaves.clear();
        for (size_t lane = 0; lane < width; ++lane) {
            const uint32_t di = visitOrder[first + lane];
            laneOfFlop[di] = 0;
            const Flop& d = m->flops[di];
            Lane& l = lanes[lane];
            auto& contributions = l.contributions;
            const auto& alreadyReconvergedInputs = l.alreadyReconvergedInputs;
            const auto& rawIds = l.rawIds;
            ++visitedDestinations;
            // The per-destination tail: its result does not depend on the order in which the batch reached the registers.
            const auto tail = [&]() {
                Influences synced;   // in name-id order of first appearance; reported sorted by name
                ++syncedStamp;
                if (syncedSlotStamp.size() < fullNames.size()) { syncedSlotStamp.resize(fullNames.size(), 0); syncedSlot.resize(fullNames.size()); }
                const auto setSynced = [&](uint32_t id, const SyncOut* so) {
                    if (syncedSlotStamp[id] == syncedStamp) { synced[syncedSlot[id]].second = so; return; }
                    syncedSlotStamp[id] = syncedStamp;
                    syncedSlot[id] = static_cast<uint32_t>(synced.size());
                    synced.emplace_back(id, so);
                };
                // Same-domain contributions in register order, the later one winning a name. A register reached by several paths
                // contributes the same each time, so ties need no order.
                std::sort(contributions.begin(), contributions.end(),
                          [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
                // A list read again only repeats the values it set before, so only its last read is applied.
                for (uint32_t i = 0; i < contributions.size(); ++i)
                    if (!std::get<2>(contributions[i])) lastUseOf[std::get<1>(contributions[i])] = i;
                for (uint32_t i = 0; i < contributions.size(); ++i) {
                    const auto [q, id, so] = contributions[i];
                    if (so) { setSynced(id, so); continue; }
                    if (lastUseOf[id] != i) continue;
                    for (const auto& [name, out] : derivedLists[id]) setSynced(name, out);
                }
                if (synced.size() + rawIds.size() < 2) return;
                // A pure downstream echo of a same-domain register that already reconverged >= 2 synced
                // influences without adding any new influence (and without directly reconverging >= 2 synced
                // influences in its own cone) does not emit a duplicate finding.
                const auto selfDerived = derived.find(d.q);
                const bool directMultiDerived = selfDerived != derived.end() && selfDerived->second.size() >= 2;
                if (rawIds.empty() && !directMultiDerived && !alreadyReconvergedInputs.empty()) {
                    const bool strictEcho = std::any_of(alreadyReconvergedInputs.begin(), alreadyReconvergedInputs.end(),
                        [&](const Influences* upstream) {
                            return upstream->size() >= synced.size() && std::all_of(synced.begin(), synced.end(), [&](const auto& entry) {
                                const auto at = std::lower_bound(upstream->begin(), upstream->end(), entry.first,
                                                                 [](const auto& e, uint32_t id) { return e.first < id; });
                                return at != upstream->end() && at->first == entry.first;
                            });
                        });
                    if (strictEcho) return;
                }
                std::set<uint32_t> doms;
                bool unresolvedSource = false;
                for (auto& [n, so] : synced) {
                    if (so->sourceIncomplete) unresolvedSource = true;
                    else doms.insert(so->srcDomain);
                }
                const char* kind = !rawIds.empty() ? "unsynchronised" : unresolvedSource ? "unresolved-source" :
                                   doms.size() > 1 ? "multi-domain" : "same-domain";
                std::string dst = stripBit(m->nameOf(d.q));
                // Avoid duplicating an exact 2-element (candidate qualifier, undecided payload) finding
                // already reported on the same (src, dst) pair by CDC-039.
                if (synced.size() == 1 && rawIds.size() == 1 && l.unresolvedCount == 1 &&
                    !unresolvedSource && l.rawDomainCount == 1 &&
                    l.firstRawDomain == synced.begin()->second->srcDomain &&
                    !cfg->disabledRules.count("CDC-039") &&
                    unresolvedQualificationPairs_.contains({symbolNames[rawIds.front()], dst}))
                    return;
                // Register order lets only the lowest-index destination of a dst + kind through, wherever it is visited.
                const std::string doneKey = dst + kind;
                const auto known = candidates.find(doneKey);
                if (known != candidates.end() && known->second.di < di) return;
                std::string list;
                std::vector<std::pair<const std::string*, const SyncOut*>> syncedByName;
                for (const auto& [id, so] : synced) syncedByName.emplace_back(&fullNames[id], so);
                std::sort(syncedByName.begin(), syncedByName.end(), [](const auto& a, const auto& b) { return *a.first < *b.first; });
                for (auto& [name, so] : syncedByName)
                    list += (list.empty() ? "" : ", ") + *name + (so->sourceIncomplete ? " (synchroniser launch source unresolved)" :
                            " (synchronised from " + (so->sourceClock.empty() ? m->domains[so->srcDomain].name : "input clock '" + so->sourceClock + "'") + ")");
                std::vector<uint32_t> rawByName = rawIds;
                std::sort(rawByName.begin(), rawByName.end(), [&](uint32_t a, uint32_t b) { return symbolNames[a] < symbolNames[b]; });
                for (const uint32_t id : rawByName) list += (list.empty() ? "" : ", ") + symbolNames[id] +
                    (hasLane(unresolvedLanes[id], lane) ? " (capture qualification unresolved)" : " (NOT synchronised)");
                Candidate cand{di, std::move(dst), GroupKey{d.proc, std::string(kind), unresolvedSource, l.unresolvedCount > 0, std::move(list)},
                               synced.size() + rawIds.size()};
                if (known != candidates.end()) known->second = std::move(cand);
                else candidates.emplace(doneKey, std::move(cand));
            };
            tail();
            l.contributions.clear();
            l.alreadyReconvergedInputs.clear();
            l.rawIds.clear();
            l.unresolvedCount = l.rawDomainCount = l.firstRawDomain = 0;
        }
        for (const uint32_t id : touchedSymbols) rawLanes[id] = unresolvedLanes[id] = Lanes{};
        touchedSymbols.clear();
        for (auto& lanesOfDomain : rawDomainLanes) lanesOfDomain = Lanes{};
        if (cfg->timings && batches % 64 == 0) {
            auto counters = cones_->telemetry();
            counters.insert(counters.end(), {{"cdc.reconvergence.visited", visitedDestinations}, {"cdc.reconvergence.destinations", visitOrder.size()},
                                             {"cdc.reconvergence.batches", batches}, {"cdc.reconvergence.components", components},
                                             {"cdc.reconvergence.edge_scans", edgeScans}, {"cdc.reconvergence.leaf_visits", leafVisits},
                                             {"cdc.reconvergence.loop_ms", static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Timings::Clock::now() - loopStart).count())},
                                             {"cdc.qualifier.calls", qualifierCalls_}});
            cfg->timings->checkpoint(counters);
        }
        first = last;
    }
    probeQualifiers_ = false;
    // Replay in register order, as the original single pass did: the first kept destination of a group sets its location.
    std::vector<Candidate*> kept;
    kept.reserve(candidates.size());
    for (auto& [doneKey, cand] : candidates) kept.push_back(&cand);
    std::sort(kept.begin(), kept.end(), [](const Candidate* a, const Candidate* b) { return a->di < b->di; });
    for (Candidate* cand : kept) {
        auto& entry = grouped[std::move(cand->key)];
        if (entry.dsts.empty()) {
            const Flop& d = m->flops[cand->di];
            entry.influenceCount = cand->influenceCount;
            entry.loc = procOf(d).loc;
            entry.hier = procOf(d).instancePath;
        }
        entry.dsts.push_back(std::move(cand->dst));
    }
    const char* rule = "CDC-010";
    for (auto& [key, entry] : grouped) {
        std::sort(entry.dsts.begin(), entry.dsts.end());
        entry.dsts.erase(std::unique(entry.dsts.begin(), entry.dsts.end()), entry.dsts.end());
        evaluated[rule]++;
        const std::string targetDesc = entry.dsts.size() == 1
            ? ("register " + entry.dsts.front() + " combines ")
            : ("registers " + entry.dsts.front() + " (and " + std::to_string(entry.dsts.size() - 1) +
               " more in the same block) combine ");
        Diagnostic x = mk(rule, Severity::Warning,
                          targetDesc + std::to_string(entry.influenceCount) +
                          (key.unresolvedSource ? " potentially asynchronous influences before sampling: " : " asynchronous influences before sampling: ") + key.list +
                          (key.unresolvedSource ? "; establish the launch relationships and review relative arrival before relying on the combined value"
                           : key.kind == "same-domain" ? "; independently synchronised signals from one domain can arrive in different cycles and reconverge inconsistently"
                           : key.kind == "multi-domain" ? "; signals from different source domains reconverge in one cone"
                           : key.hasUnresolvedQual ? "; capture-control necessity has not been established"
                                                   : "; unsynchronised signals are combined by logic, so glitches/skew are sampled"),
                          entry.loc, entry.hier,
                          key.unresolvedSource || key.hasUnresolvedQual ? Confidence::Unresolved : Confidence::Heuristic);
        x.signals = entry.dsts;
        if (entry.dsts.size() > 1) {
            const size_t limit = std::min<size_t>(entry.dsts.size(), 16);
            for (size_t i = 0; i < limit; ++i)
                x.evidence.push_back({"affected_register", entry.dsts[i], entry.loc});
            if (entry.dsts.size() > 16)
                x.evidence.push_back({"affected_register", "... and " + std::to_string(entry.dsts.size() - 16) + " more", {}});
        }
        x.evidence.push_back({"reconvergence_kind", key.kind, {}});
        if (key.hasUnresolvedQual)
            x.evidence.push_back({"proof_obligation", "establish the unresolved capture qualification before deciding which asynchronous influences can be sampled together", {}});
        x.proofState = ProofState::NotAttempted;
        x.nextStep = "Merge the related signals in the source domain and synchronise once (or use a handshake/FIFO) before combining them.";
        add(std::move(x));
    }
}

// --- clocks built from logic: muxes, gates, divided and generated clocks --------------------------------------------
namespace {
// Latch processes whose first guard is a unary negation (`if (!clk)`, `if (~clk)`): transparent while low.
struct LowPhaseLatches : ASTVisitor<LowPhaseLatches, VisitFlags::AllGood> {
    const slang::SourceManager& sm;
    std::set<std::string> keys;
    explicit LowPhaseLatches(const slang::SourceManager& s) : sm(s) {}
    void handle(const ProceduralBlockSymbol& p) {
        if (p.procedureKind == ProceduralBlockKind::AlwaysLatch || p.procedureKind == ProceduralBlockKind::Always) {
            const Statement* b = &p.getBody();
            while (b && (b->kind == StatementKind::Timed || b->kind == StatementKind::Block || b->kind == StatementKind::List)) {
                if (b->kind == StatementKind::Timed) b = &b->as<TimedStatement>().stmt;
                else if (b->kind == StatementKind::Block) b = &b->as<BlockStatement>().body;
                else { auto& l = b->as<StatementList>().list; b = l.empty() ? nullptr : l[0]; }
            }
            if (b && b->kind == StatementKind::Conditional && !b->as<ConditionalStatement>().conditions.empty()) {
                const Expression* e = b->as<ConditionalStatement>().conditions[0].expr;
                while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
                bool low = false;
                if (e->kind == ExpressionKind::UnaryOp) { auto op = e->as<UnaryExpression>().op; low = op == UnaryOperator::LogicalNot || op == UnaryOperator::BitwiseNot; }
                const auto* clock = e->kind == ExpressionKind::UnaryOp ? e->as<UnaryExpression>().operand().getSymbolReference() : e->getSymbolReference();
                if (low && clock) {
                    auto l = p.location;
                    auto key = sm.getFullPath(l.buffer()).generic_string() + ":" + std::to_string(sm.getLineNumber(l)) + ":" + std::to_string(sm.getColumnNumber(l));
                    keys.insert(key);
                }
            }
        }
        visitDefault(p);
    }
};
} // namespace

bool hasExactInputSelfHold(const Model& model, NodeId target) {
    const auto* symbol = model.nodes[target].sym;
    if (!symbol) return false;
    bool found = false;
    for (const auto& write : model.proceduralWrites) {
        if (write.target != symbol) continue;
        if (found || !write.value || !write.exactTarget || write.partialTarget || write.called) return false;
        const auto& value = strip(*write.value);
        if (value.kind != ExpressionKind::ConditionalOp) return false;
        const auto& mux = value.as<ConditionalExpression>();
        if (!stageOperand(mux.left(), model, target) && !stageOperand(mux.right(), model, target)) return false;
        found = true;
    }
    return found;
}

bool hasExactStageComparison(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactStageComparisonImpl(model, target, stage, next, idleValue);
}
bool hasExactStageXor(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactStageExprDetector(model, target, stage, next, exactXor, idleValue);
}
bool hasExactStageInvertedAnd(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactStageExprDetector(model, target, stage, next, exactInvertedAnd, idleValue);
}
bool hasExactCombinationalComparison(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                     NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactCombinationalComparisonImpl(model, stageEdge, nextEdge, target, stage, next, idleValue);
}
bool hasExactCombinationalXor(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                              NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactCombinationalXorImpl(model, stageEdge, nextEdge, target, stage, next, idleValue);
}
bool hasExactCombinationalInvertedAnd(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                      NodeId target, NodeId stage, NodeId next, char* idleValue) {
    return hasExactCombinationalInvertedAndImpl(model, stageEdge, nextEdge, target, stage, next, idleValue);
}

void Shared::clockStructure() {
    LowPhaseLatches lowLatches(*design_->compilation->getSourceManager());
    design_->compilation->getRoot().visit(lowLatches);
    std::set<NodeId> clockRoots;
    for (auto& dm : m->domains) clockRoots.insert(m->aliasRoot(dm.root));
    // primary inputs declared as clock sources in the SDC, or named like clocks (heuristic), are clock leaves too
    for (NodeId n = 0; n < m->nodes.size(); n++) {
        if (n >= m->topInput.size() || !m->topInput[n] || !m->nodes[n].sym) continue;
        std::string nm = lower(std::string(m->nodes[n].sym->name));
        if (!rel_->clockNameOfNode(n).empty() || nm.find("clk") != std::string::npos || nm.find("clock") != std::string::npos) clockRoots.insert(m->aliasRoot(n));
    }
    using WriteTarget = std::pair<const slang::ast::InstanceSymbol*, const slang::ast::ValueSymbol*>;
    std::map<WriteTarget, const ProceduralWrite*> toggleWrites;
    for (const auto& w : m->proceduralWrites) {
        if (!w.target || !w.value || !w.nonBlocking || w.called || w.partialTarget ||
            !w.exactTarget || w.conditionalDepth || w.afterPossibleExit) continue;
        const auto& value = strip(*w.value);
        if (value.kind != ExpressionKind::UnaryOp) continue;
        const auto& unary = value.as<UnaryExpression>();
        if ((unary.op == UnaryOperator::BitwiseNot || unary.op == UnaryOperator::LogicalNot) &&
            unary.operand().getSymbolReference() == w.target)
            toggleWrites.emplace(WriteTarget{w.instance, w.target}, &w);
    }
    for (const auto& w : m->proceduralWrites) {
        const auto candidate = toggleWrites.find({w.instance, w.target});
        if (candidate != toggleWrites.end() && candidate->second != &w) toggleWrites.erase(candidate);
    }
    auto dividerDomain = [&](NodeId node) -> int {
        const auto* flop = m->flopAt(node);
        if (!flop || !flop->clockResolved || m->symbolWidth(node) != 1 || m->multiSequential.count(node) ||
            m->uncertainCallDrivers.count(node)) return -1;
        const auto& bit = m->nodes[node];
        return toggleWrites.contains({bit.inst, bit.sym}) ? static_cast<int>(flop->domain) : -1;
    };
    auto domainOfRoot = [&](NodeId n) -> int {
        for (size_t j = 0; j < m->domains.size(); j++)
            if (m->domains[j].root == n) return static_cast<int>(j);
        // An inferred divider's launch clock is only used for enable comparison.
        return dividerDomain(n);
    };

    // CST-013: generated-clock source and bounded RTL derivation consistency
    {
        std::map<std::string, const sdc::ClockDef*> latestClocks;
        for (const auto& ck : cfg->constraints.clocks) {
            const std::string nm = ck.name.empty() ? (ck.sources.empty() ? std::string() : ck.sources[0].pattern) : ck.name;
            if (!nm.empty()) latestClocks[nm] = &ck;
        }
        auto resolveRefNodes = [&](const sdc::ObjectRef& ref) {
            std::set<NodeId> out;
            if (ref.regexp || ref.unsupportedOption) return out;
            for (NodeId n = 0; n < m->nodes.size(); ++n) {
                if (rel_->nameMatches(ref.kind, ref.pattern, n, stripBit(m->nameOf(n))) ||
                    rel_->bitMatches(ref.kind, ref.pattern, n))
                    out.insert(n);
            }
            return out;
        };
        // Walk exact combinational alias/inversion chain from `start`, stopping at a source root, a toggle divider flop,
        // another known clock/top-input root, or any non-alias/non-inversion logic.
        struct DerivedTrace {
            enum class Kind { AliasOrInvert, ToggleDivider, OtherSource, Unprovable };
            Kind kind = Kind::Unprovable;
            int divideBy = 1;
            bool inverted = false;
            NodeId sourceNode = kNoNode;
        };
        auto traceGenerated = [&](NodeId start, const std::set<NodeId>& declaredSourceRoots) -> DerivedTrace {
            NodeId cur = start;
            bool inv = false;
            int totalDiv = 1;
            std::set<NodeId> seen;
            for (int hops = 0; hops < 64; ++hops) {
                if (!seen.insert(cur).second) return {};
                const NodeId ar = m->aliasRoot(cur);
                if (declaredSourceRoots.count(ar)) {
                    return {totalDiv == 1 ? DerivedTrace::Kind::AliasOrInvert : DerivedTrace::Kind::ToggleDivider,
                            totalDiv, inv, ar};
                }
                // Check toggle divider flop (single-bit q <= ~q)
                if (const int ddom = dividerDomain(cur); ddom >= 0) {
                    if (totalDiv >= 256) return {};
                    totalDiv *= 2;
                    cur = m->domains[ddom].root;
                    continue;
                }
                // If we reached a different flop (non-toggle) or a black box, derivation is unprovable
                if (m->flopAt(cur)) return {};
                if (cur < m->blackBoxDriven.size() && m->blackBoxDriven[cur]) return {};
                // If we reached a top-level input port (not in declaredSourceRoots), proven source mismatch!
                if (cur < m->topInput.size() && m->topInput[cur]) {
                    return {DerivedTrace::Kind::OtherSource, totalDiv, inv, ar};
                }
                // Follow single exact alias or single Not edge
                if (cur >= m->in.size() || m->in[cur].empty()) return {};
                NodeId next = kNoNode;
                bool stepInv = false;
                bool ok = true;
                for (auto ei : m->in[cur]) {
                    const Edge& e = m->edges[ei];
                    if (e.type != EdgeType::Comb || e.ctrl || e.approximate) { ok = false; break; }
                    if (e.exact) {
                        if (next != kNoNode && next != e.from) { ok = false; break; }
                        next = e.from;
                    } else if (e.flip || e.op == Op::Not) {
                        if (next != kNoNode) { ok = false; break; }
                        next = e.from;
                        stepInv = true;
                    } else {
                        ok = false; break;
                    }
                }
                if (!ok || next == kNoNode) return {};
                inv ^= stepInv;
                cur = next;
            }
            return {};
        };

        for (const auto& [ckName, ckPtr] : latestClocks) {
            const auto& ck = *ckPtr;
            if (!ck.generated) continue;
            std::set<NodeId> sourceRoots;
            for (const auto& srcRef : ck.sourceObjects)
                for (NodeId n : resolveRefNodes(srcRef)) sourceRoots.insert(m->aliasRoot(n));
            std::set<NodeId> targets;
            for (const auto& tgtRef : ck.sources)
                for (NodeId n : resolveRefNodes(tgtRef)) targets.insert(n);
            if (sourceRoots.empty() || targets.empty()) continue; // CST-002 reports unresolved selectors

            bool hasUnsupportedTransform = !ck.multiplyBy.empty();
            bool declInvert = false;
            for (const auto& opt : ck.waveformOptions) {
                if (opt == "-invert") declInvert = true;
                else if (opt != "-combinational") hasUnsupportedTransform = true;
            }
            int declDiv = 1;
            bool declDivValid = true;
            if (!ck.divideBy.empty()) {
                char* end = nullptr;
                long v = std::strtol(ck.divideBy.c_str(), &end, 10);
                if (!end || *end != '\0' || v < 1 || v > 1000000) declDivValid = false;
                else declDiv = static_cast<int>(v);
            }

            auto srcLabel = [&](NodeId id) {
                const std::string r = rel_->rtlBitName(id);
                return !r.empty() ? r : stripBit(m->nameOf(id));
            };
            for (NodeId tgt : targets) {
                evaluated["CST-013"]++;
                const std::string tgtName = stripBit(m->nameOf(tgt));
                const DerivedTrace tr = traceGenerated(tgt, sourceRoots);
                if (tr.kind == DerivedTrace::Kind::OtherSource) {
                    const std::string actualSrc = srcLabel(tr.sourceNode);
                    const std::string declSrc = !ck.sourceObjects.empty() ? ck.sourceObjects.front().pattern : ck.masterClock;
                    Diagnostic d = mk("CST-013", Severity::Error,
                                      "generated clock '" + ckName + "' declares -source '" + declSrc +
                                      "' but RTL target '" + tgtName + "' is driven by '" + actualSrc + "'",
                                      ck.loc, {}, Confidence::Established);
                    d.signals = {tgtName, actualSrc};
                    d.nextStep = "Fix the create_generated_clock -source or target object so it matches the RTL clock tree.";
                    add(std::move(d));
                    incomplete("constraints", "generated clock '" + ckName + "' source disagrees with RTL");
                } else if (hasUnsupportedTransform || !declDivValid || tr.kind == DerivedTrace::Kind::Unprovable) {
                    Diagnostic d = mk("CST-013", Severity::Warning,
                                      "generated clock '" + ckName + "' derivation on '" + tgtName +
                                      "' cannot be established from bounded RTL analysis",
                                      ck.loc, {}, Confidence::Unresolved);
                    d.signals = {tgtName};
                    d.nextStep = "Review the RTL clock divider/mux/multiplier against the create_generated_clock declaration.";
                    add(std::move(d));
                    incomplete("constraints", "generated clock '" + ckName + "' derivation is unverified");
                } else if (tr.divideBy != declDiv || tr.inverted != declInvert) {
                    const std::string actualSrc = srcLabel(tr.sourceNode);
                    Diagnostic d = mk("CST-013", Severity::Error,
                                      "generated clock '" + ckName + "' declares divide_by " + std::to_string(declDiv) +
                                      (declInvert ? " -invert" : "") + " from '" + actualSrc +
                                      "', but RTL target '" + tgtName + "' derives divide_by " + std::to_string(tr.divideBy) +
                                      (tr.inverted ? " (inverted)" : ""),
                                      ck.loc, {}, Confidence::Established);
                    d.signals = {tgtName, actualSrc};
                    d.nextStep = "Align create_generated_clock options (-divide_by/-invert) with the RTL derivation.";
                    add(std::move(d));
                    incomplete("constraints", "generated clock '" + ckName + "' derivation disagrees with RTL");
                } else {
                    const std::string actualSrc = srcLabel(tr.sourceNode);
                    Diagnostic d = mk("CST-013", Severity::Info,
                                      "generated clock '" + ckName + "' on '" + tgtName + "' matches bounded RTL derivation from '" +
                                      actualSrc + "' (divide_by " + std::to_string(tr.divideBy) +
                                      (tr.inverted ? ", inverted" : "") + ")",
                                      ck.loc, {}, Confidence::Established);
                    d.signals = {tgtName, actualSrc};
                    add(std::move(d));
                }
            }
        }
    }
    for (size_t i = 0; i < m->domains.size(); i++) {
        const Domain& dom = m->domains[i];
        // SDC generated clock on this root: the master/generated relationship is declared but not derived
        if (!domainClockName(static_cast<uint32_t>(i)).empty())
            for (auto& ck : cfg->constraints.clocks)
                if (ck.generated && (ck.name.empty() ? (ck.sources.empty() ? std::string() : ck.sources[0].pattern) : ck.name) == domainClockName(static_cast<uint32_t>(i))) {
                    evaluated["CDC-030"]++;
                    Diagnostic d = mk("CDC-030", Severity::Warning,
                                      "clock '" + dom.name + "' is declared with create_generated_clock (master '" + ck.masterClock + "', divide_by '" + ck.divideBy +
                                      "', multiply_by '" + ck.multiplyBy + "'): edge alignment between master and generated clock is a timing-tool assumption; "
                                      "crossings between them stay UNKNOWN until a clock relation is declared",
                                      ck.loc, {}, Confidence::Unresolved);
                    d.nextStep = "State the relationship explicitly (set_clock_groups) or synchronise crossings between master and generated clock.";
                    add(std::move(d));
                    incomplete("clock-relationships", "generated clock '" + dom.name + "' phase relationship is assumed, not derived");
                }
        if (dom.kind != RootKind::Logic) continue;
        // walk the combinational cone of the clock net
        std::vector<NodeId> clockLeaves, flopLeaves, inputLeaves;
        bool latch = false, bbox = false, directEnable = false;
        std::map<std::pair<NodeId, uint32_t>, std::set<NodeId>> latchControls;
        // Keep toggle-shaped state as an enable unless used in a mux data
        // arm or there is no already identified clock in the gate cone.
        std::map<NodeId, bool> dividerCandidates;
        enum class ClockPath { Alias, MuxData, OtherLogic };
        using ClockWalk = std::tuple<NodeId, bool, ClockPath>; // node, latch path, clock path
        std::set<ClockWalk> seen{{dom.root, false, ClockPath::Alias}};
        std::vector<ClockWalk> st{{dom.root, false, ClockPath::Alias}};
        while (!st.empty()) {
            auto [n, viaLatch, path] = st.back(); st.pop_back();
            const bool muxData = path == ClockPath::MuxData;
            const NodeId root = m->aliasRoot(n);
            if (root != dom.root && clockRoots.count(root)) { clockLeaves.push_back(root); continue; }
            if (m->flopOfQ.count(n)) {
                if ((!viaLatch || muxData) && dividerDomain(n) >= 0) {
                    dividerCandidates[n] |= muxData;
                    continue;
                }
                flopLeaves.push_back(n); directEnable = directEnable || !viaLatch; continue;
            }
            if (n < m->topInput.size() && m->topInput[n]) { inputLeaves.push_back(n); directEnable = directEnable || !viaLatch; continue; }
            if (n < m->blackBoxDriven.size() && m->blackBoxDriven[n]) bbox = true;
            const auto incoming = m->in[n];
            const bool gatesKnownClock = path != ClockPath::OtherLogic &&
                std::any_of(incoming.begin(), incoming.end(), [&](auto ei) {
                    const auto& edge = m->edges[ei];
                    return edge.type == EdgeType::Comb && !edge.ctrl &&
                        (edge.op == Op::And || edge.op == Op::Or) &&
                        clockRoots.count(m->aliasRoot(edge.from));
                });
            for (auto ei : incoming) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                bool isLatch = m->procs[e.proc].kind == ProcKind::Latch;
                if (isLatch) {
                    auto& controls = latchControls[{n, e.proc}];
                    if (e.ctrl) {
                        const auto& pl = m->procs[e.proc].loc;
                        const auto key = pl.file + ":" + std::to_string(pl.line) + ":" + std::to_string(pl.column);
                        const bool low = lowLatches.keys.count(key) && e.op == Op::Not &&
                            e.inv == 1 && e.branch == Branch::Taken && !e.approximate;
                        controls.insert(low ? m->aliasRoot(e.from) : kNoNode);
                    }
                }
                if (isLatch && e.ctrl) continue;   // the latch's own control edge is not an enable path
                ClockPath nextPath = ClockPath::OtherLogic;
                if (path != ClockPath::OtherLogic && !gatesKnownClock && !isLatch && !e.ctrl && !e.approximate) {
                    if (e.op == Op::Mux) nextPath = ClockPath::MuxData;
                    else if (e.exact) nextPath = path;
                }
                const ClockWalk next{e.from, viaLatch || isLatch, nextPath};
                if (seen.insert(next).second) st.push_back(next);
            }
        }
        const bool needsClock = clockLeaves.empty();
        for (const auto& [node, muxData] : dividerCandidates) {
            if (needsClock || muxData) clockLeaves.push_back(node);
            else { flopLeaves.push_back(node); directEnable = true; }
        }
        std::sort(clockLeaves.begin(), clockLeaves.end());
        clockLeaves.erase(std::unique(clockLeaves.begin(), clockLeaves.end()), clockLeaves.end());
        // A low-phase latch is sufficient only when it controls this clock,
        // every enable path passes through a latch, and the final gate is a
        // positive AND. Preserve direct bypass hazards and wrong polarity.
        latch = !directEnable && !latchControls.empty() && clockLeaves.size() == 1 &&
            std::all_of(latchControls.begin(), latchControls.end(), [&](const auto& entry) {
                const auto& controls = entry.second;
                return controls.size() == 1 && *controls.begin() == clockLeaves.front();
            });
        if (latch) {
            const auto drivers = m->in[dom.root];
            const bool directClockOperand = std::any_of(drivers.begin(), drivers.end(), [&](auto edge) {
                return m->aliasRoot(m->edges[edge].from) == clockLeaves.front();
            });
            latch = directClockOperand && std::all_of(drivers.begin(), drivers.end(), [&](auto edge) {
                const auto& e = m->edges[edge];
                return e.type == EdgeType::Comb && e.op == Op::And && e.inv == 0;
            });
        }
        const SourceLocation& loc = m->nodes[dom.root].sym ? SourceLocation{} : SourceLocation{};
        (void)loc;
        // relationship between a register's clock domain and a clock leaf (which may not be the root of any domain)
        auto relLeaf = [&](uint32_t flopDomain, NodeId leaf) {
            int j = domainOfRoot(leaf);
            if (j >= 0) return relation(flopDomain, static_cast<uint32_t>(j));
            return rel_->relationToName(flopDomain, rel_->clockNameOfNode(leaf));
        };
        auto leafName = [&](NodeId leaf) { int j = domainOfRoot(leaf); return j >= 0 ? m->domains[j].name : m->nameOf(leaf); };
        auto gateLoc = [&]() {
            const auto drivers = m->in[dom.root];
            if (drivers.empty()) return SourceLocation{};
            const auto proc = m->edges[*drivers.begin()].proc;
            if (std::any_of(drivers.begin(), drivers.end(), [&](auto edge) { return m->edges[edge].proc != proc; }))
                return SourceLocation{};
            return m->procs[proc].loc;
        };
        auto selectInfo = [&](std::vector<NodeId>& cl, std::string& what) {
            for (auto n : flopLeaves) what += (what.empty() ? "" : ", ") + m->nameOf(n);
            for (auto n : inputLeaves) what += (what.empty() ? "" : ", ") + m->nameOf(n) + " (primary input)";
            (void)cl;
        };
        if (clockLeaves.size() >= 2) {
            evaluated["CDC-019"]++; evaluated["CDC-031"]++;
            std::string sel;
            selectInfo(clockLeaves, sel);
            Diagnostic d = mk("CDC-019", Severity::Warning,
                              "clock '" + dom.name + "' is built from " + std::to_string(clockLeaves.size()) + " clocks by combinational logic (clock mux/combine)" +
                              (sel.empty() ? "" : "; select/enable inputs: " + sel) + ": it is not a recognised glitch-free clock multiplexer",
                              {}, {}, Confidence::Unresolved);
            d.nextStep = "Use a library glitch-free clock mux / clock-gating cell, or constrain the select so it only changes while the clocks are stopped.";
            for (auto cl : clockLeaves) d.evidence.push_back({"clock_input", m->nameOf(cl), {}});
            d.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
            add(std::move(d));
            incomplete("clock-extraction", "clock '" + dom.name + "' is a clock mux/combine of unknown glitch behaviour");
            // select domain versus the clocks it switches between, and the output clock versus its inputs
            std::string issue;
            for (auto n : flopLeaves) {
                const Flop& sf = m->flops[m->flopOfQ.at(n)];
                if (!sf.clockResolved) { issue += "select " + m->nameOf(n) + " has an unidentified clock; "; continue; }
                for (auto cl : clockLeaves)
                    if (relLeaf(sf.domain, cl) != Relation::Same)
                        issue += "select " + m->nameOf(n) + " (domain " + m->domains[sf.domain].name + ") is not synchronous to " + leafName(cl) + "; ";
            }
            for (auto n : inputLeaves) issue += "select " + m->nameOf(n) + " is a primary input with no declared relation to the clocks; ";
            for (auto cl : clockLeaves)
                if (relLeaf(static_cast<uint32_t>(i), cl) == Relation::Unknown) issue += "relation of output clock to " + leafName(cl) + " is unknown; ";
            if (!issue.empty()) {
                Diagnostic x = mk("CDC-031", Severity::Warning, "clock mux '" + dom.name + "': " + issue + "the selection crosses clock domains", {}, {}, Confidence::Unresolved);
                x.identityKey = dom.name;
                x.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
                x.nextStep = "Synchronise the select into each input clock domain (handshaked switch) and declare the output clock relationships.";
                add(std::move(x));
            }
        } else if (clockLeaves.size() == 1 && (!flopLeaves.empty() || !inputLeaves.empty() || bbox)) {
            evaluated["CDC-020"]++;
            std::string sel;
            selectInfo(clockLeaves, sel);
            if (bbox) {
                Diagnostic d = mk("CDC-002", Severity::Warning, "clock '" + dom.name + "' is gated through an unmodeled black box: its relationship to the source clock is unresolved", {}, {}, Confidence::Unresolved);
                add(std::move(d));
            }
            for (auto n : flopLeaves) {
                const Flop& sf = m->flops[m->flopOfQ.at(n)];
                Relation rel = sf.clockResolved ? relLeaf(sf.domain, clockLeaves[0]) : Relation::Unknown;
                if (rel != Relation::Same) {
                    Diagnostic d = mk("CDC-020", rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                                      "enable " + m->nameOf(n) + " of the gated clock '" + dom.name + "' is launched in another clock domain [" + relName(rel) +
                                      "]: the gate can glitch the clock", procOf(sf).loc, procOf(sf).instancePath,
                                      rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                    d.identityKey = dom.name + ":" + stripBit(m->nameOf(n));
                    d.evidence.push_back({"site_signal", m->nameOf(n), procOf(sf).loc});
                    d.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
                    d.nextStep = "Synchronise the enable to the gated clock's source domain and hold it with a latch-based clock gate.";
                    add(std::move(d));
                }
            }
            for (auto n : inputLeaves) {
                auto d = mk("CDC-020", Severity::Warning, "gated clock '" + dom.name + "' enable " + m->nameOf(n) + " is a primary input with no declared relation to the clock",
                       {}, {}, Confidence::Unresolved);
                d.identityKey = dom.name + ":" + stripBit(m->nameOf(n));
                d.evidence.push_back({"site_signal", m->nameOf(n), gateLoc()});
                d.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
                add(std::move(d));
            }
            if (!latch && (!flopLeaves.empty() || !inputLeaves.empty())) {
                evaluated["CDC-045"]++;
                Diagnostic d = mk("CDC-045", Severity::Warning,
                                  "clock '" + dom.name + "' is gated by combinational logic without an enable latch: a late or changing enable (" + sel +
                                  ") can truncate or glitch the clock pulse", {}, {}, Confidence::Heuristic);
                d.identityKey = dom.name;
                d.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
                d.nextStep = "Use a latch-based integrated clock gate (enable latched while the clock is low).";
                add(std::move(d));
            } else if (latch) {
                evaluated["CDC-045"]++;
                Diagnostic d = mk("CDC-045", Severity::Info, "clock '" + dom.name + "' is gated through a latch that is transparent while the clock is low (clock-gate pattern)",
                                  {}, {}, Confidence::Heuristic);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"trust_model", "every enable path must pass through the latch and the enable must be stable while the clock is high; timing of the latch is not analysed", {}});
                d.evidence.push_back({"site_signal", m->nameOf(dom.root), gateLoc()});
                add(std::move(d));
            }
        }
    }
    // A generated clock output is still a clock mux when no register in this
    // elaborated design consumes it. The domain-root walk above cannot see
    // such a net because Model::domains is collected from sequential users.
    // Keep this boundary check narrow: a top-level output must be named as a
    // clock and its cone must reach two independently identified clock leaves.
    std::set<NodeId> domainRoots;
    for (const auto& dom : m->domains) domainRoots.insert(m->aliasRoot(dom.root));
    std::set<std::string> seenClockOutputs;
    for (NodeId root = 0; root < m->nodes.size(); ++root) {
        if (root >= m->topOutput.size() || !m->topOutput[root] || !m->nodes[root].sym || domainRoots.count(m->aliasRoot(root))) continue;
        const std::string name = std::string(m->nodes[root].sym->name);
        if (!seenClockOutputs.insert(stripBit(m->nameOf(root))).second) continue;
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowerName.find("clk") == std::string::npos && lowerName.find("clock") == std::string::npos) continue;
        std::set<NodeId> seen{root}, leaves, controls;
        std::vector<NodeId> work{root};
        while (!work.empty()) {
            NodeId n = work.back(); work.pop_back();
            if (n != root && clockRoots.count(n)) { leaves.insert(n); continue; }
            bool advanced = false;
            for (auto ei : m->in[n]) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                advanced = true;
                if (e.ctrl) controls.insert(e.from);
                if (seen.insert(e.from).second) work.push_back(e.from);
            }
            if (!advanced && n != root) controls.insert(n);
        }
        if (leaves.size() < 2) continue;
        evaluated["CDC-019"]++;
        evaluated["CDC-031"]++;
        Diagnostic mux = mk("CDC-019", Severity::Warning,
            "clock output '" + m->nameOf(root) + "' is built from " + std::to_string(leaves.size()) +
            " clocks by combinational logic (clock mux/combine); it is not a recognised glitch-free clock multiplexer",
            {}, {}, Confidence::Unresolved);
        mux.signals = {stripBit(m->nameOf(root))};
        for (NodeId leaf : leaves) mux.evidence.push_back({"clock_input", m->nameOf(leaf), {}});
        mux.nextStep = "Use a library glitch-free clock mux or constrain its select so it only changes while both clocks are stopped.";
        add(std::move(mux));
        std::string selectNames;
        for (NodeId control : controls) {
            if (!selectNames.empty()) selectNames += ", ";
            selectNames += m->nameOf(control);
        }
        Diagnostic select = mk("CDC-031", Severity::Warning,
            "clock mux '" + m->nameOf(root) + "' has no observed domain for its control" +
            (selectNames.empty() ? std::string() : " (" + selectNames + ")") +
            "; selection safety relative to the clock inputs is unresolved",
            {}, {}, Confidence::Unresolved);
        select.signals = {stripBit(m->nameOf(root))};
        for (NodeId control : controls) select.evidence.push_back({"mux_control", m->nameOf(control), {}});
        select.identityKey = stripBit(m->nameOf(root));
        add(std::move(select));
        incomplete("clock-extraction", "clock output '" + stripBit(m->nameOf(root)) + "' is a combinational mux with no consuming register");
    }
}


// --- CDC-043: rising/falling-edge registers of one clock ---------------------------------------------------------------
void Shared::edgeStructure() {
    for (auto& [names, nodes] : halfCycle_) {
        evaluated["CDC-043"]++;
        const Flop& d = m->flops[m->flopOfQ.at(nodes.second)];
        Diagnostic x = mk("CDC-043", Severity::Warning,
                          "path " + names.first + " -> " + names.second + " connects registers on opposite edges of clock '" + m->domains[d.domain].name +
                          "': it has half a clock period; as part of a synchroniser it halves the settling time", procOf(d).loc, procOf(d).instancePath, Confidence::Heuristic);
        x.signals = {names.first, names.second};
        x.nextStep = "Confirm the half-cycle timing budget, or use the same edge for both registers.";
        add(std::move(x));
    }
    std::set<std::string> dualReported;
    for (auto& f : m->flops)
        if (f.edge == 3 && f.clockResolved) {
            const std::string regName = stripBit(flopName(f));
            if (!dualReported.insert(regName).second) continue;
            evaluated["CDC-043"]++;
            Diagnostic x = mk("CDC-043", Severity::Warning, "register " + regName + " is triggered on both clock edges (dual-edge): edge relationships are not modelled",
                              procOf(f).loc, procOf(f).instancePath, Confidence::Unresolved);
            x.signals = {regName};
            x.nextStep = "Review edge timing and metastability containment for the dual-edge register, or use single-edge flops in synchronisers.";
            add(std::move(x));
            incomplete("clock-relationships", "dual-edge register " + regName);
        }
}

} // namespace sverity::cdc

namespace sverity::cdc {

namespace {
struct AnnotatedFinder : slang::ast::ASTVisitor<AnnotatedFinder, slang::ast::VisitFlags::AllGood> {
    std::vector<std::pair<std::string, std::string>> found;   // (hierarchical instance path, definition name)
    void handle(const slang::ast::InstanceSymbol& inst) {
        if (auto* syn = inst.getDefinition().getSyntax(); syn && syn->kind == slang::syntax::SyntaxKind::ModuleDeclaration) {
            auto& md = syn->as<slang::syntax::ModuleDeclarationSyntax>();
            for (auto* ai : md.attributes)
                for (auto* spec : ai->specs) {
                    std::string n = lower(std::string(spec->name.valueText()));
                    if (n == "sverity_sync" || n == "cdc_sync") found.push_back({inst.getHierarchicalPath(), std::string(inst.getDefinition().name)});
                }
        }
        visitDefault(inst);
    }
};
} // namespace

// --- protocol / metadata checks over the crossing groups --------------------------------------------------------------
void Shared::protocols() {
    bool unsupportedRegexpException = false;
    bool unsupportedThroughException = false;
    for (const auto& exception : cfg->constraints.exceptions) {
        const auto hasRegexp = [](const std::vector<sdc::ObjectRef>& refs) {
            return std::any_of(refs.begin(), refs.end(), [](const auto& ref) {
                return ref.regexp;
            });
        };
        unsupportedRegexpException = unsupportedRegexpException || hasRegexp(exception.from) ||
            hasRegexp(exception.to) || hasRegexp(exception.through);
        unsupportedThroughException = unsupportedThroughException || !exception.through.empty();
    }
    if (unsupportedRegexpException && !cfg->disabledRules.count("CDC-038"))
        incomplete("constraints", "CDC-038 does not resolve regexp SDC path-exception selectors; matching paths remain unclassified");
    if (unsupportedThroughException && !cfg->disabledRules.count("CDC-038"))
        incomplete("constraints", "CDC-038 does not path-evaluate -through SDC path-exception selectors; matching paths remain unclassified");

    auto isSyncedSingle = [&](const Group& g) {
        if (g.collapsed || g.dstFlops.size() != 1) return false;
        const Chain& ch = chains_[g.dstFlops[0]];
        return !ch.callEffectsUnknown && !ch.linkLogic && !ch.feedbackWithFanout && ch.depth >= 2;
    };
    auto withTag = [&](const Group& g) {
        Relation rel = relation(g.srcDomain, g.dstDomain);
        return std::string(rel == Relation::Unknown ? " [clock relationship UNKNOWN]" : "");
    };
    auto ev = [&](Diagnostic& d, const Group& g) {
        d.signals = {g.srcName, g.dstName};
        d.identityKey = crossingIdentity(g);
        d.crossing = crossingOf(g);
        if (relation(g.srcDomain, g.dstDomain) == Relation::Exclusive) d.evidence.push_back({"trust_model", "the two clocks are declared exclusive by constraint (trusted, not verified)", {}});
        d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", {}});
        d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ")", {}});
    };
    std::vector<const Group*> single;   // recognised single-bit synchronised crossings
    std::map<const Group*, Diagnostic> toggleObligations;   // CDC-024: resolved after handshakes, which absorb their toggle legs
    std::map<const Group*, Diagnostic> missingToggleDetectors;
    std::map<const Group*, std::vector<Diagnostic>> pulseWarnings;
    std::map<const Group*, Diagnostic> pending041;
    std::set<const Group*> rdc023CoveredLegs;
    auto reset041 = [&](const Group& g, const Flop* sf, const std::string& tag) {
        bool exclusive = relation(g.srcDomain, g.dstDomain) == Relation::Exclusive; (void)exclusive;
    if (sf) {
            auto roots = [&](const Flop& x) { return asyncResetSignature(x); };
            bool different = false;
            bool sourceResetOnly = false;
            bool pairedEndpoints = false;
            // Keep endpoint correspondence. A bus group is not a Cartesian
            // product of every source reset and every destination reset.
            for (const auto& [sourceQ, firstQ] : g.sourceCaptures) {
                const Flop* source = m->flopAt(sourceQ);
                auto firstIt = m->flopOfQ.find(firstQ);
                if (!source || firstIt == m->flopOfQ.end()) continue;
                const Chain& chain = chains_[firstIt->second];
                if (chain.stages.empty()) continue;
                pairedEndpoints = true;
                const Flop& output = m->flops[chain.stages[outputStage(chain)]];
                const auto rs = roots(*source), rd = roots(output);
                different = different || (!rs.empty() && !rd.empty() && rs != rd);
                sourceResetOnly = sourceResetOnly || (!rs.empty() && rd.empty());
            }
            if (!pairedEndpoints) {
                const auto rs = roots(*sf);
                const auto& chain = chains_[g.dstFlops[0]];
                const auto rd = roots(m->flops[chain.stages[outputStage(chain)]]);
                different = !rs.empty() && !rd.empty() && rs != rd;
                sourceResetOnly = !rs.empty() && rd.empty();
            }
            const bool mismatch = different;
            if (mismatch) {
                evaluated["CDC-041"]++;
                Diagnostic d = mk("CDC-041", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + ": the source and the synchroniser are reset by different resets; "
                                  "while one side is in reset the other sees stale or unreleased values" + tag, g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Reset both sides together or define the release order (see RDC-023).";
                pending041.emplace(&g, std::move(d));
            } else if (sourceResetOnly) {
                evaluated["CDC-041"]++;
                Diagnostic d = mk("CDC-041", Severity::Info, "crossing " + g.srcName + " -> " + g.dstName + ": the source is reset, the synchroniser is not: after a source-only reset the destination keeps "
                                  "the old value for the synchroniser latency" + tag, g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                pending041.emplace(&g, std::move(d));
            }
        }
    };
    for (auto& [key, g] : groups_) {
        Relation rel = relation(g.srcDomain, g.dstDomain);
        bool exclusive = rel == Relation::Exclusive;
        const Flop& f0 = m->flops[g.dstFlops[0]];
        std::string tag = withTag(g);

        // CDC-038: SDC exceptions cut the crossing but say nothing about synchronisation
        if (g.srcQ != kNoNode) {
            std::set<const sdc::PathException*> seenExceptions;
            std::vector<const sdc::PathException*> matchedExceptions;
            auto matchEndpoint = [&](NodeId source, NodeId capture) {
                for (auto* ex : exceptionsFor(source, capture, true, sdc::PathException::Kind::FalsePath))
                    if (seenExceptions.insert(ex).second) matchedExceptions.push_back(ex);
            };
            if (g.sourceCaptures.empty()) matchEndpoint(g.srcQ, f0.q);
            else for (const auto& [source, capture] : g.sourceCaptures) matchEndpoint(source, capture);
            for (auto* ex : matchedExceptions) {
                evaluated["CDC-038"]++;
                const char* kind = ex->kind == sdc::PathException::Kind::FalsePath ? "set_false_path" : ex->kind == sdc::PathException::Kind::Multicycle ? "set_multicycle_path"
                                 : ex->kind == sdc::PathException::Kind::MaxDelay ? "set_max_delay" : "set_min_delay";
                if (ex->kind == sdc::PathException::Kind::MaxDelay || ex->kind == sdc::PathException::Kind::MinDelay) continue;   // constraints that keep the path timed
                Diagnostic d = mk("CDC-038", Severity::Warning,
                                  std::string("crossing ") + g.srcName + " -> " + g.dstName + " is cut by " + kind + " (" + ex->loc.file + ":" + std::to_string(ex->loc.line) +
                                  "): an exception removes timing checks, it does not make the crossing safe (a synchroniser is " +
                                  (isSyncedSingle(g) || (!g.collapsed && !chains_[g.dstFlops[0]].callEffectsUnknown && !chains_[g.dstFlops[0]].feedbackWithFanout && chains_[g.dstFlops[0]].depth >= 2) ? "present" : "NOT recognised") + ")",
                                  g.loc, g.hier, Confidence::Established);
                ev(d, g);
                d.nextStep = "Keep synchroniser/protocol evidence independent of the exception, and justify the exception.";
                add(std::move(d));
            }
        }
        // CDC-037: declared relationship versus observed synchroniser
        if (exclusive && !g.collapsed && !chains_[g.dstFlops[0]].dualEdgeStage && !chains_[g.dstFlops[0]].callEffectsUnknown && !chains_[g.dstFlops[0]].feedbackWithFanout && chains_[g.dstFlops[0]].depth >= 2) {
            evaluated["CDC-037"]++;
            Diagnostic d = mk("CDC-037", Severity::Warning, "clocks " + m->domains[g.srcDomain].name + " and " + m->domains[g.dstDomain].name +
                              " are declared exclusive but a synchroniser was recognised between them: the constraint and the structure disagree", g.loc, g.hier);
            ev(d, g);
            add(std::move(d));
        }
        // CDC-035: memories crossing domains
        if (g.memory && !g.collapsed) {
            evaluated["CDC-035"]++;
            Diagnostic d = mk("CDC-035", exclusive ? Severity::Info : Severity::Warning,
                              "memory " + g.srcName + " -> " + g.dstName + " is written and read in different clock domains: correctness depends on the pointer/handshake protocol around it" + tag,
                              g.loc, g.hier, Confidence::Unresolved);
            ev(d, g);
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Guard dual-clock memory access with a recognised async-FIFO pointer scheme; vendor RAM semantics (read-during-write) are not modelled.";
            add(std::move(d));
        }
        if (!g.collapsed && g.dstFlops.size() > 1 && !g.memory && g.srcQ != kNoNode && !chains_[g.dstFlops[0]].linkLogic && !chains_[g.dstFlops[0]].callEffectsUnknown && !chains_[g.dstFlops[0]].feedbackWithFanout && chains_[g.dstFlops[0]].depth >= 2)
            reset041(g, m->flopAt(g.srcQ), tag);   // multi-bit synchronised groups get the reset check too
        const bool recognised = isSyncedSingle(g);
        if ((!recognised && !chains_[g.dstFlops[0]].feedbackWithFanout) || g.memory) continue;
        NodeId sq = g.srcQ;
        const Flop* sf = m->flopAt(sq);
        const bool stuckSource = sq != kNoNode && m->stuckFlopValues.contains(sq);
        if (recognised && !chains_[g.dstFlops[0]].dualEdgeStage && !stuckSource) single.push_back(&g);

        // CDC-012: synchroniser attributes (profile dependent)
        {
            int marked = 0, total = 0;
            const auto& synchronizer = chains_[g.dstFlops[0]];
            const size_t attributeStages = synchronizer.edgeDetectorBoundary
                ? std::min(synchronizer.edgeDetectorBoundary, synchronizer.stages.size())
                : synchronizer.stages.size();
            for (size_t index = 0; index < attributeStages; ++index) {
                const auto st = synchronizer.stages[index];
                total++;
                auto* sym = m->nodes[m->flops[st].q].sym;
                if (sym && hasSyncAttribute(*design_->compilation, *sym)) marked++;
            }
            evaluated["CDC-012"]++;
            bool fpga = cfg->profile == "fpga";
            if (marked && marked < total) {
                Diagnostic d = mk("CDC-012", Severity::Warning, "synchroniser " + g.dstName + ": only " + std::to_string(marked) + " of " + std::to_string(total) +
                                  " stages carry the ASYNC_REG/sverity_sync attribute", g.loc, g.hier);
                ev(d, g);
                add(std::move(d));
            } else if (!marked && fpga) {
                Diagnostic d = mk("CDC-012", Severity::Warning, "synchroniser " + g.dstName + " has no ASYNC_REG attribute on its stages (profile fpga): placement and "
                                  "timing tools may not keep the stages together", g.loc, g.hier);
                ev(d, g);
                d.nextStep = "Mark the synchroniser registers with (* ASYNC_REG = \"TRUE\" *).";
                add(std::move(d));
            }
        }

        // CDC-013 / CDC-042: pulses and the clock-rate relation
        const bool monotonicState = sf && monotonicSelfFeedback(*m, sq);
        if (!exclusive && sf && !monotonicState && !stuckSource) {
            evaluated["CDC-013"]++;
            bool eventLike = false;
            for (auto ei : m->in[sq]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Seq && !e.exact && !e.ctrl) eventLike = true;
            }
            double ts = periodNs(g.srcDomain), td = periodNs(g.dstDomain);
            bool fast = ts > 0 && td > 0 && ts < td * 1.5;
            if (ts <= 0 || td <= 0) {   // rates only from create_clock periods: unknown is a WARNING, never "slow enough"
                Diagnostic d = mk("CDC-013", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + ": the clock periods are not constrained, so a fast-to-slow pulse loss cannot be excluded" + tag,
                                  g.loc, g.hier, Confidence::Unresolved);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Constrain both clocks with create_clock -period.";
                pulseWarnings[&g].push_back(std::move(d));
            }
            if (ts > 0 && td > 0 && fast) {
                Diagnostic d = mk("CDC-013", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + " has source period (" + std::to_string(ts) +
                                  " ns) comparable to or shorter than the destination period (" + std::to_string(td) + " ns): a pulse or short level can be missed" + tag, g.loc, g.hier);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Hold the signal for at least two destination periods, or use a toggle/handshake synchroniser.";
                pulseWarnings[&g].push_back(std::move(d));
            } else if (eventLike) {
                bool unknownRate = ts <= 0 || td <= 0;
                Diagnostic d = mk("CDC-013", unknownRate ? Severity::Warning : Severity::Info,
                                  "crossing " + g.srcName + " -> " + g.dstName + " carries a computed (event-like) signal: nothing guarantees it is held long enough for the destination" +
                                  (unknownRate ? " (clock periods unknown)" : "") + tag, g.loc, g.hier,
                                  unknownRate ? Confidence::Unresolved : Confidence::Heuristic);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"signal_kind", "computed (event-like) signal", {}});
                d.evidence.push_back({"proof_obligation", "minimum pulse width >= 1.5 x destination period (or a toggle/handshake protocol) is not verified", {}});
                pulseWarnings[&g].push_back(std::move(d));
            }
        }

        // CDC-014: toggle (event) synchroniser
        if (sf && m->selfLogic.count(sq) && !monotonicState && !stuckSource) {
            evaluated["CDC-014"]++;
            const Chain& ch = chains_[g.dstFlops[0]];
            const bool detector = ch.edgeDetector;
            if (g.anyLogicBefore) {
                Diagnostic d = mk("CDC-014", Severity::Warning, "toggle register " + g.srcName + " is not launched directly: logic between the toggle flop and the synchroniser can glitch" + tag, g.loc, g.hier);
                ev(d, g);
                add(std::move(d));
            }
            if (!detector) {
                Diagnostic d = mk("CDC-014", Severity::Warning, "toggle-like register " + g.srcName + " is synchronised to " + g.dstName +
                                  " but no destination edge detector was found; if this crossing encodes events, the receiver sees a level rather than an event" + tag,
                                  g.loc, g.hier, Confidence::Unresolved);
                ev(d, g);
                d.evidence.push_back({"protocol_intent", "the RTL shows self-dependent state but does not establish whether it is an event toggle or a persistent level/handshake", {}});
                d.nextStep = "Confirm whether the signal represents events; add a destination edge detector for event toggles, or document the level/handshake protocol.";
                missingToggleDetectors.emplace(&g, std::move(d));
            } else {
                bool observedAtDepth = false;
                bool resetValuesDiffer = false;
                bool resetValuesUnknown = false;
                bool detectorHasReset = false;
                bool stageHasIff = false;
                auto validDetectorFlop = [&](const Flop& df, const Flop& refStage, char idleValue) {
                    if (!df.clockResolved || df.blocking || df.hasIff || !df.otherDomains.empty() ||
                        m->multiSequential.count(df.q) || relation(df.domain, refStage.domain) != Relation::Same ||
                        df.edge == 3 || df.edge != refStage.edge)
                        return false;
                    bool ok = true;
                    for (size_t i = 0; i < df.resets.size(); ++i) {
                        const char rv = i < df.resetValues.size() ? df.resetValues[i] : '?';
                        if (rv != '0' && rv != '1') { resetValuesUnknown = true; ok = false; }
                        else if (rv != idleValue) { resetValuesDiffer = true; ok = false; }
                    }
                    for (const auto& fact : df.syncResetFacts) {
                        if (fact.forcedValue != '0' && fact.forcedValue != '1') { resetValuesUnknown = true; ok = false; }
                        else if (fact.forcedValue != idleValue) { resetValuesDiffer = true; ok = false; }
                    }
                    return ok;
                };
                auto observable = [&](NodeId start, const Flop& refStage, char idleValue) {
                    std::vector<NodeId> pending{start};
                    std::unordered_set<NodeId> visited;
                    while (!pending.empty() && visited.size() < 256) {
                        const NodeId node = pending.back();
                        pending.pop_back();
                        if (!visited.insert(node).second) continue;
                        if (const Flop* df = m->flopAt(node)) {
                            if (df->syncDataSuppressed || !validDetectorFlop(*df, refStage, idleValue)) continue;
                        } else if (node != start) {
                            if (m->constDrivers.contains(node)) continue;
                            size_t liveCombInputs = 0;
                            for (auto inId : m->in[node])
                                if (!m->isDeadEdge(m->edges[inId])) ++liveCombInputs;
                            if (liveCombInputs != 1) continue;
                        } else {
                            if (m->constDrivers.contains(node)) continue;
                        }
                        if (m->topOutput[node]) return true;
                        for (auto edgeId : m->out[node]) {
                            const auto& edge = m->edges[edgeId];
                            if (!((edge.type == EdgeType::Comb || edge.type == EdgeType::Seq) &&
                                  edge.exact && !edge.ctrl && !edge.approximate && !edge.shared &&
                                  !m->isDeadEdge(edge) && !m->isConstantProc(edge.proc)))
                                continue;
                            if (edge.type == EdgeType::Seq) {
                                const Flop* df = m->flopAt(edge.to);
                                if (!df || df->syncDataSuppressed || !validDetectorFlop(*df, refStage, idleValue))
                                    continue;
                                bool singleUnconditional = true;
                                size_t liveSeqInputs = 0;
                                for (auto inId : m->in[edge.to]) {
                                    const auto& inEdge = m->edges[inId];
                                    if (inEdge.type != EdgeType::Seq || m->isDeadEdge(inEdge)) continue;
                                    ++liveSeqInputs;
                                    if (inEdge.from != node || !inEdge.exact || inEdge.ctrl ||
                                        inEdge.approximate || inEdge.shared)
                                        singleUnconditional = false;
                                }
                                if (!singleUnconditional || liveSeqInputs != 1) continue;
                            }
                            pending.push_back(edge.to);
                        }
                    }
                    return false;
                };
                const size_t requiredOut = outputStage(ch);
                size_t lastRequiredStage = requiredOut;
                for (const auto& site : ch.detectorSites) {
                    const auto earlier = site.earlierStage;
                    if (earlier + 1 >= ch.stages.size()) continue;
                    lastRequiredStage = std::max(lastRequiredStage, earlier + 1);
                    const Flop& before = m->flops[ch.stages[earlier]];
                    const Flop& after = m->flops[ch.stages[earlier + 1]];
                    observedAtDepth |= site.depth >= minStages_ && before.edge != 3 && before.edge == after.edge &&
                                       !before.hasIff && !after.hasIff &&
                                       asyncResetSignature(before) == asyncResetSignature(after) &&
                                       observable(site.target, before, site.idleValue);
                }
                {
                    char asynchronousValue = '?';
                    std::map<std::pair<NodeId, bool>, char> synchronousValues;
                    auto compareValue = [&](char value, char& reference) {
                        detectorHasReset = true;
                        if (value != '0' && value != '1') resetValuesUnknown = true;
                        else if (reference == '?') reference = value;
                        else if (reference != value) resetValuesDiffer = true;
                    };
                    auto inspectResetValues = [&](const Flop& f) {
                        for (size_t i = 0; i < f.resets.size(); ++i)
                            compareValue(i < f.resetValues.size() ? f.resetValues[i] : '?', asynchronousValue);
                        for (const auto& fact : f.syncResetFacts) {
                            auto [entry, inserted] = synchronousValues.try_emplace({aliasRoot(fact.node), fact.activeLow}, '?');
                            compareValue(fact.forcedValue, entry->second);
                        }
                    };
                    inspectResetValues(*sf);
                    for (size_t stage = 0; stage <= lastRequiredStage && stage < ch.stages.size(); ++stage) {
                        const Flop& st = m->flops[ch.stages[stage]];
                        stageHasIff |= st.hasIff;
                        inspectResetValues(st);
                    }
                }
                const bool usableChain = observedAtDepth && !ch.dualEdgeStage && !ch.clockMismatch && !ch.resetMismatch &&
                                         !stageHasIff && !resetValuesDiffer && !resetValuesUnknown;
                const std::string reason = resetValuesDiffer ? "different reset values can create a spurious event"
                    : resetValuesUnknown ? "compatible reset values are not established"
                    : "detector observation, required stage depth, edge timing or asynchronous reset compatibility is not established";
                Diagnostic d = mk("CDC-014", usableChain ? Severity::Info : Severity::Warning,
                                  (usableChain ? "toggle synchroniser recognised for "
                                               : "toggle edge detector found for ") +
                                  g.srcName + " -> " + g.dstName + (usableChain ? "" : ": " + reason) + tag, g.loc, g.hier,
                                  usableChain ? Confidence::Heuristic : Confidence::Unresolved);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({usableChain ? "trust_model" : "proof_obligation",
                                      usableChain ? "toggles must be at least two destination clock periods apart, otherwise events are lost"
                                                  : "review the required stages and prove toggle delivery and reset coordination", {}});
                add(std::move(d));
                Diagnostic o = mk("CDC-024", Severity::Info, "functional correctness of the toggle protocol " + g.srcName + " -> " + g.dstName + " is not verified", g.loc, g.hier, Confidence::Heuristic);
                o.identityKey = g.srcName + ":" + g.dstName;
                o.proofState = ProofState::NotAttempted;
                toggleObligations.emplace(&g, std::move(o));
                // RDC-023 (c): a toggle's two sides reset apart: resetting one alone changes the level the other compares
                // with, which creates or loses an event. Asynchronous roots and the synchronous-reset candidate both count,
                // through every synchronizer and detected delayed stage.
                const auto resetsOf = [&](const Flop& x) {
                    std::set<NodeId> r;
                    for (auto& rr : x.resets) r.insert(rr.root.node);
                    for (const auto& fact : x.syncResetFacts) if (fact.node != kNoNode) r.insert(aliasRoot(fact.node));
                    if (x.syncResetFacts.empty() && x.syncReset != kNoNode) r.insert(aliasRoot(x.syncReset));
                    return r;
                };
                const auto syncSig = [&](const Flop& x) {
                    std::set<std::pair<NodeId, bool>> r;
                    for (const auto& fact : x.syncResetFacts)
                        if (fact.node != kNoNode) r.emplace(aliasRoot(fact.node), fact.activeLow);
                    if (x.syncResetFacts.empty() && x.syncReset != kNoNode)
                        r.emplace(aliasRoot(x.syncReset), x.syncActiveLow);
                    return r;
                };
                const Flop& out = m->flops[ch.stages[requiredOut]];
                const auto source = resetsOf(*sf), receiver = resetsOf(out);
                const auto sourceAsync = asyncResetSignature(*sf);
                const auto sourceSync = syncSig(*sf);
                const auto sameResetAsSource = [&](const Flop& x) {
                    return asyncResetSignature(x) == sourceAsync && syncSig(x) == sourceSync;
                };
                bool stageResetsMatch = (!sourceAsync.empty() || !sourceSync.empty()) && !ch.resetMismatch && sameResetAsSource(out);
                for (size_t stage = 0; stage <= lastRequiredStage && stage < ch.stages.size(); ++stage) {
                    const Flop& st = m->flops[ch.stages[stage]];
                    detectorHasReset |= !resetsOf(st).empty();
                    stageResetsMatch &= sameResetAsSource(st);
                }
                if (!source.empty() || !receiver.empty() || detectorHasReset) {
                    evaluated["RDC-023"]++;
                    const bool together = stageResetsMatch && !resetValuesDiffer && !resetValuesUnknown;
                    Diagnostic r = mk("RDC-023", together ? Severity::Info : Severity::Warning,
                                      "toggle protocol " + g.srcName + " -> " + g.dstName + (resetValuesDiffer || resetValuesUnknown
                                          ? ": " + reason : together
                                          ? ": both sides are reset by the same signals; the order in which their clock domains leave reset is still not defined by the design"
                                          : ": the source and the receiver are not reset together, so resetting one side alone can create a spurious event or lose one") + tag,
                                      g.loc, g.hier, together ? Confidence::Heuristic : Confidence::Unresolved);
                    ev(r, g);
                    r.proofState = ProofState::NotAttempted;
                    r.evidence.push_back({together ? "trust_model" : "proof_obligation",
                                          together ? "a shared reset is assumed to reach both domains before either side toggles again"
                                                   : "show that the two sides are only reset together, or that the receiver discards the event a one-sided reset creates", {}});
                    add(std::move(r));
                }
            }
        }

        reset041(g, sf, tag);
    }

    // CDC-015: request/acknowledge loops between two synchronised single-bit crossings in opposite directions
    struct HandshakeReach { std::set<NodeId> flops, direct, indirect; bool exhausted = false; bool uncertain = false; };
    std::map<NodeId, HandshakeReach> handshakeReach;
    size_t handshakeWork = 0;
    constexpr size_t kHandshakeReachBudget = 100000;
    constexpr size_t kHandshakeReachPerSource = 4096;
    size_t handshakeBudget = kHandshakeReachBudget;
    try { handshakeBudget = static_cast<size_t>(std::stoull(cfg->option("CDC-015.reach_budget", "100000"))); }
    catch (...) { handshakeBudget = kHandshakeReachBudget; }
    auto reachFor = [&](NodeId q) -> const HandshakeReach& {
        auto [it, inserted] = handshakeReach.try_emplace(q);
        if (!inserted) return it->second;
        auto start = m->flopOfQ.find(q);
        if (start == m->flopOfQ.end()) return it->second;
        std::vector<uint32_t> pending{start->second};
        std::set<uint32_t> seen{start->second};
        while (!pending.empty()) {
            if (handshakeWork >= handshakeBudget || seen.size() >= kHandshakeReachPerSource) {
                it->second.exhausted = true;
                incomplete("cdc-015-handshake-reachability", "CDC-015 same-domain endpoint reachability hit a per-source or shared work limit; affected pairings are Unresolved");
                break;
            }
            const uint32_t current = pending.back();
            pending.pop_back();
            ++handshakeWork;
            const auto cone = coneOf(m->flops[current]);
            if (!cone->unresolved.empty() || !cone->bbLeaves.empty() || !cone->uncertainCalls.empty()) {
                it->second.uncertain = true;
                incomplete("cdc-015-handshake-reachability", "CDC-015 same-domain endpoint reachability encountered an uncertain cone; affected pairings are Unresolved");
            }
            for (const auto& source : cone->flops) {
                auto fi = m->flopOfQ.find(source.q);
                if (fi == m->flopOfQ.end()) continue;
                const uint32_t index = fi->second;
                if (index != start->second && m->flops[index].domain == m->flops[start->second].domain) {
                    it->second.flops.insert(source.q);
                    if (current == start->second) it->second.direct.insert(source.q);
                    else it->second.indirect.insert(source.q);
                }
                if (m->flops[index].domain == m->flops[start->second].domain && seen.insert(index).second)
                    pending.push_back(index);
            }
        }
        for (NodeId direct : it->second.direct) it->second.indirect.erase(direct);
        return it->second;
    };
    // The elaborated model is already flattened: hierarchy is provenance, not a
    // channel boundary. Pair only exact launch endpoints whose reciprocal state
    // cones reach the other leg's synchronizer. Require a unique reciprocal
    // partner so aliases, shared controls and independent instances cannot be
    // silently combined into one protocol.
    using EndpointKey = std::tuple<NodeId, uint32_t, uint32_t>;
    struct HandshakeEndpoint { EndpointKey key; std::vector<const Group*> groups; std::set<NodeId> stages; };
    std::map<EndpointKey, HandshakeEndpoint> endpointMap;
    for (const Group* g : single) {
        if (g->srcQ == kNoNode || g->dstFlops.empty()) continue;
        EndpointKey key{g->srcQ, g->srcDomain, g->dstDomain};
        auto& endpoint = endpointMap[key];
        endpoint.key = key;
        endpoint.groups.push_back(g);
        for (auto di : g->dstFlops) {
            if (!haveChain_[di]) { chains_[di] = chainFor(di); haveChain_[di] = 1; }
            for (auto stage : chains_[di].stages) endpoint.stages.insert(m->flops[stage].q);
        }
    }
    std::vector<HandshakeEndpoint*> endpoints;
    for (auto& [key, endpoint] : endpointMap) endpoints.push_back(&endpoint);
    struct StageOwner { HandshakeEndpoint* endpoint; const Group* group; };
    std::map<NodeId, std::vector<StageOwner>> endpointByStage;
    for (auto* endpoint : endpoints)
        for (const Group* group : endpoint->groups)
            for (auto di : group->dstFlops)
                for (auto stage : chains_[di].stages)
                    endpointByStage[m->flops[stage].q].push_back({endpoint, group});
    struct HandshakeRelation {
        HandshakeEndpoint* a; HandshakeEndpoint* b;
        bool aFromB = false, bFromA = false;
        bool aFromBDirect = false, bFromADirect = false;
        bool aFromBIndirect = false, bFromAIndirect = false;
        bool indirect = false, exhausted = false, uncertain = false;
        std::set<std::pair<const Group*, const Group*>> exactPairs;
    };
    std::map<std::pair<EndpointKey, EndpointKey>, HandshakeRelation> relationMap;
    for (auto* source : endpoints) {
        const auto& [q, srcDomain, dstDomain] = source->key;
        const HandshakeReach& reach = reachFor(q);
        for (const Group* sourceGroup : source->groups) for (NodeId reached : reach.flops) {
            auto found = endpointByStage.find(reached);
            if (found == endpointByStage.end()) continue;
            for (const auto& owner : found->second) {
                auto* target = owner.endpoint;
                const auto& [targetQ, targetSrc, targetDst] = target->key;
                if (srcDomain != targetDst || dstDomain != targetSrc || q == targetQ) continue;
                EndpointKey lo = source->key < target->key ? source->key : target->key;
                EndpointKey hi = source->key < target->key ? target->key : source->key;
                auto& relation = relationMap[{lo, hi}];
                relation.a = source->key == lo ? source : target;
                relation.b = source->key == lo ? target : source;
                const bool direct = reach.direct.contains(reached);
                if (source->key == lo) {
                    relation.bFromA = true;
                    relation.bFromADirect = relation.bFromADirect || direct;
                    relation.bFromAIndirect = relation.bFromAIndirect || !direct;
                } else {
                    relation.aFromB = true;
                    relation.aFromBDirect = relation.aFromBDirect || direct;
                    relation.aFromBIndirect = relation.aFromBIndirect || !direct;
                }
                relation.exhausted = relation.exhausted || reach.exhausted;
                relation.uncertain = relation.uncertain || reach.uncertain;
                if (sourceGroup) relation.exactPairs.insert(source->key == lo ? std::make_pair(sourceGroup, owner.group) : std::make_pair(owner.group, sourceGroup));
            }
        }
    }
    std::vector<HandshakeRelation> handshakeRelations;
    std::map<EndpointKey, unsigned> relatedCount;
    std::map<EndpointKey, unsigned> reciprocalCount;
    std::vector<HandshakeRelation*> oneWayRelations;
    auto isAsyncResetReleaseBridge = [&](NodeId q) {
        const Flop* sf = m->flopAt(q);
        return sf && sf->resets.size() == 1 && sf->resetTransitionKnown &&
               sf->releasedSource == kNoNode &&
               (sf->releasedValue == '0' || sf->releasedValue == '1') &&
               !sf->resetValues.empty() &&
               (sf->resetValues[0] == '0' || sf->resetValues[0] == '1') &&
               sf->resetValues[0] != sf->releasedValue &&
               !m->multiSequential.count(q) && !m->uncertainCallDrivers.count(q);
    };
    for (auto& [key, relation] : relationMap) {
        relation.indirect = (relation.aFromBIndirect && !relation.aFromBDirect) ||
                            (relation.bFromAIndirect && !relation.bFromADirect);
        const bool oneWayIndirectResetBridge =
            !relation.exhausted && !relation.uncertain && relation.indirect &&
            ((relation.aFromB && !relation.bFromA && isAsyncResetReleaseBridge(std::get<0>(relation.a->key))) ||
             (relation.bFromA && !relation.aFromB && isAsyncResetReleaseBridge(std::get<0>(relation.b->key))));
        if (oneWayIndirectResetBridge) continue;
        handshakeRelations.push_back(relation);
        if (relation.aFromB && relation.bFromA) {
            reciprocalCount[relation.a->key]++;
            reciprocalCount[relation.b->key]++;
        } else {
            oneWayRelations.push_back(&relation);
        }
    }
    for (const auto& [endpoint, count] : reciprocalCount)
        relatedCount[endpoint] = count;
    for (const auto* relation : oneWayRelations) {
        if (reciprocalCount[relation->a->key] && reciprocalCount[relation->b->key]) {
            relatedCount[relation->a->key]++;
            relatedCount[relation->b->key]++;
        } else {
            if (!reciprocalCount[relation->a->key]) relatedCount[relation->a->key]++;
            if (!reciprocalCount[relation->b->key]) relatedCount[relation->b->key]++;
        }
    }
    std::vector<std::pair<const Group*, const Group*>> recognizedHandshakes;
    auto exactPairLess = [](const auto& x, const auto& y) {
        return std::tie(x.first->srcName, x.first->dstName, x.first->hier, x.second->srcName, x.second->dstName, x.second->hier) <
               std::tie(y.first->srcName, y.first->dstName, y.first->hier, y.second->srcName, y.second->dstName, y.second->hier);
    };
    for (auto& relation : handshakeRelations) {
        const HandshakeEndpoint* ae = relation.a;
        const HandshakeEndpoint* be = relation.b;
        const Group* a = ae->groups.front();
        const Group* b = be->groups.front();
        if (!relation.exactPairs.empty()) {
            const auto& pair = *std::min_element(relation.exactPairs.begin(), relation.exactPairs.end(), exactPairLess);
            a = pair.first;
            b = pair.second;
        }
        evaluated["CDC-015"]++;
        std::vector<std::pair<const Group*, const Group*>> reciprocalGroups;
        for (const auto& [ga, gb] : relation.exactPairs) {
            const auto& forward = reachFor(ga->srcQ);
            const auto& reverse = reachFor(gb->srcQ);
            bool forwards = false, returns = false;
            for (auto di : gb->dstFlops)
                for (auto stage : chains_[di].stages)
                    forwards |= forward.flops.contains(m->flops[stage].q);
            for (auto di : ga->dstFlops)
                for (auto stage : chains_[di].stages)
                    returns |= reverse.flops.contains(m->flops[stage].q);
            relation.exhausted = relation.exhausted || forward.exhausted || reverse.exhausted;
            relation.uncertain = relation.uncertain || forward.uncertain || reverse.uncertain;
            if (forwards && returns && !forward.exhausted && !forward.uncertain &&
                !reverse.exhausted && !reverse.uncertain)
                reciprocalGroups.emplace_back(ga, gb);
        }
        const bool reciprocalRelation = relation.aFromB && relation.bFromA;
        if (relatedCount[ae->key] != 1 || relatedCount[be->key] != 1 ||
            (reciprocalRelation && reciprocalGroups.size() != 1) || relation.indirect || relation.exhausted || relation.uncertain) {
            Diagnostic d = mk("CDC-015", Severity::Warning,
                              "possible request/acknowledge pairing between " + a->srcName + " and " + b->srcName +
                              ((relation.exhausted || relation.uncertain) ? " has incomplete or uncertain dependency search; pairing is Unresolved" :
                               relation.indirect ? " depends on bounded same-domain state reachability; no handshake is established" :
                               " is ambiguous across elaborated endpoints; no handshake is established"),
                              a->loc, a->hier, Confidence::Unresolved);
            d.signals = {a->srcName, b->srcName};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"proof_obligation", "identify the owning request and acknowledge endpoints and prove their control qualification", {}});
            add(std::move(d));
            if (relation.aFromB && relation.bFromA) {
                Diagnostic o = mk("CDC-024", Severity::Warning,
                                  "functional correctness of the ambiguously paired handshake " + a->srcName + " <-> " + b->srcName + " is not verified",
                                  a->loc, a->hier, Confidence::Unresolved);
                o.signals = {a->srcName, b->srcName};
                o.identityKey = a->srcName + ":" + b->srcName + ":ambiguous-handshake";
                o.proofState = ProofState::NotAttempted;
                o.evidence.push_back({"proof_obligation", "resolve endpoint ownership, control qualification and reset behavior before claiming a protocol pair", {}});
                for (const Group* leg : {a, b}) {
                    const uint32_t capture = leg->dstFlops.front();
                    o.evidence.push_back({"handshake_leg", leg->srcName + " -> " + flopName(m->flops[capture]) + " (" + m->domains[leg->srcDomain].name + " -> " + m->domains[leg->dstDomain].name + ")", leg->loc});
                    o.evidence.push_back({"capture_register", flopName(m->flops[capture]), procOf(m->flops[capture]).loc});
                }
                evaluated["CDC-024"]++;
                add(std::move(o));
            }
            if ((relation.aFromB && relation.bFromA) || relation.exhausted || relation.uncertain) {
                const bool searchIncomplete = relation.exhausted || relation.uncertain;
                Diagnostic r = mk("RDC-023", Severity::Warning,
                                  "reset-release sequencing for the " +
                                  std::string(searchIncomplete ? "search-incomplete candidate handshake " : "ambiguously paired handshake ") +
                                  a->srcName + " <-> " + b->srcName + " is unresolved",
                                  a->loc, a->hier, Confidence::Unresolved);
                r.signals = {a->srcName, b->srcName};
                r.proofState = ProofState::NotAttempted;
                r.evidence.push_back({"handshake_leg", a->srcName + " -> " + flopName(m->flops[a->dstFlops.front()]) + " (" + m->domains[a->srcDomain].name + " -> " + m->domains[a->dstDomain].name + ")", a->loc});
                r.evidence.push_back({"handshake_leg", b->srcName + " -> " + flopName(m->flops[b->dstFlops.front()]) + " (" + m->domains[b->srcDomain].name + " -> " + m->domains[b->dstDomain].name + ")", b->loc});
                r.evidence.push_back({"proof_obligation",
                                      searchIncomplete
                                          ? "complete the endpoint dependency search and show coordinated reset release before either side acts"
                                          : "resolve endpoint ownership and show coordinated reset release before either side acts",
                                      {}});
                r.identityKey = a->srcName + ":" + b->srcName +
                    (searchIncomplete ? ":incomplete-handshake-reset" : ":ambiguous-handshake-reset");
                evaluated["RDC-023"]++;
                add(std::move(r));
            }
            continue;
        }
        if (reciprocalGroups.size() == 1) {
            a = reciprocalGroups.front().first;
            b = reciprocalGroups.front().second;
        } else if (!relation.exactPairs.empty()) {
            const auto& pair = *std::min_element(relation.exactPairs.begin(), relation.exactPairs.end(), exactPairLess);
            a = pair.first;
            b = pair.second;
        } else continue;
        const bool ackFollowsReq = relation.aFromB;
        const bool reqFollowsAck = relation.bFromA;
        if (ackFollowsReq && reqFollowsAck) {
            recognizedHandshakes.emplace_back(a, b);
            // A request/acknowledge toggle is consumed as a level until
            // the peer acknowledges it. Its handshake obligation covers
            // event spacing and its lack of a standalone edge detector.
            if (!cfg->disabledRules.contains("CDC-024")) for (const Group* leg : {a, b}) {
                missingToggleDetectors.erase(leg);
                pulseWarnings.erase(leg);
            }
            Diagnostic d = mk("CDC-015", Severity::Info, "request/acknowledge loop recognised: " + a->srcName + " (" + m->domains[a->srcDomain].name + ") <-> " + b->srcName +
                              " (" + m->domains[b->srcDomain].name + ")", a->loc, a->hier, Confidence::Heuristic);
            d.signals = {a->srcName, b->srcName};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"trust_model", "the 4-phase protocol (req held until ack, ack held until req drops, data stable while req is high) is not verified", {}});
            add(std::move(d));
            Diagnostic o = mk("CDC-024", Severity::Info, "functional correctness of the handshake " + a->srcName + " <-> " + b->srcName + " is not verified", a->loc, a->hier, Confidence::Heuristic);
            o.identityKey = a->srcName + ":" + b->srcName;
            o.proofState = ProofState::NotAttempted;
            for (const Group* leg : {a, b}) {
                const uint32_t capture = leg->dstFlops.front();
                o.evidence.push_back({"handshake_leg", leg->srcName + " -> " + flopName(m->flops[capture]) + " (" + m->domains[leg->srcDomain].name + " -> " + m->domains[leg->dstDomain].name + ")", leg->loc});
                o.evidence.push_back({"capture_register", flopName(m->flops[capture]), procOf(m->flops[capture]).loc});
            }
            // the toggle legs of a two-phase handshake are this one structure: one obligation covers them
            for (const Group* leg : {a, b})
                if (toggleObligations.erase(leg))
                    o.evidence.push_back({"covers_toggle", leg->srcName + " -> " + leg->dstName, leg->loc});
            evaluated["CDC-024"]++;
            add(std::move(o));
        } else if (ackFollowsReq != reqFollowsAck) {
            const Group* req = ackFollowsReq ? a : b;
            Diagnostic d = mk("CDC-015", Severity::Warning, "possible request/acknowledge mismatch: " + (ackFollowsReq ? a->srcName : b->srcName) +
                              " is acknowledged by " + (ackFollowsReq ? b->srcName : a->srcName) + ", but the request does not depend on the acknowledge (it is never deasserted by it)",
                              req->loc, req->hier, Confidence::Heuristic);
            d.signals = {a->srcName, b->srcName};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
        }
    }

    // A global reachability cap can be reached before any endpoint stage was
    // enumerated. Preserve the unresolved pairing and reset obligations per
    // clock direction even when only one rule family is selected.
    std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>> unenumerated;
    std::set<EndpointKey> endpointsWithRelations;
    for (const auto& relation : handshakeRelations) {
        endpointsWithRelations.insert(relation.a->key);
        endpointsWithRelations.insert(relation.b->key);
    }
    for (auto* endpoint : endpoints) {
        const auto& [q, srcDomain, dstDomain] = endpoint->key;
        const auto& reach = handshakeReach.at(q);
        if (reach.exhausted || reach.uncertain) {
            if (!endpointsWithRelations.contains(endpoint->key))
                unenumerated[{srcDomain, dstDomain}].push_back(endpoint->groups.front()->srcName);
        }
    }
    for (auto& [direction, names] : unenumerated) {
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        std::string listed;
        for (size_t i = 0; i < std::min<size_t>(names.size(), 8); ++i) listed += (i ? ", " : "") + names[i];
        evaluated["CDC-015"]++;
        Diagnostic d = mk("CDC-015", Severity::Warning,
                          "handshake endpoint search is incomplete for " + listed + (names.size() > 8 ? " and " + std::to_string(names.size() - 8) + " more" : "") +
                          "; no definite pairing or mismatch can be reported",
                          {}, {}, Confidence::Unresolved);
        d.proofState = ProofState::NotAttempted;
        d.evidence.push_back({"proof_obligation", "complete bounded endpoint dependency search to determine whether a reciprocal handshake exists", {}});
        add(std::move(d));
        evaluated["RDC-023"]++;
        auto reset = mk("RDC-023", Severity::Warning,
                        "handshake endpoint search is incomplete for " + listed +
                        (names.size() > 8 ? " and " + std::to_string(names.size() - 8) + " more" : "") +
                        "; reset release coordination cannot be evaluated without a pairing",
                        {}, {}, Confidence::Unresolved);
        reset.proofState = ProofState::NotAttempted;
        reset.evidence.push_back({"proof_obligation", "identify any reciprocal handshake and verify reset release coordination", {}});
        add(std::move(reset));
    }

    for (auto& [group, warning] : missingToggleDetectors) add(std::move(warning));
    for (auto& [group, warnings] : pulseWarnings)
        for (auto& warning : warnings) add(std::move(warning));

    for (auto& [g, o] : toggleObligations) { evaluated["CDC-024"]++; add(std::move(o)); }

    // CDC-047: one launch bit synchronised more than once into one domain; the copies can disagree for a cycle
    {
        std::map<std::pair<NodeId, uint32_t>, std::map<uint32_t, const Group*>> capturesOf;
        for (auto& [key, g] : groups_) {
            if (g.collapsed) continue;
            evaluated["CDC-047"] += g.srcBits.size();
            for (auto di : g.dstFlops) {
                const Chain& chain = chains_[di];
                if (chain.callEffectsUnknown || chain.linkLogic || chain.feedbackWithFanout || chain.depth < 2) continue;
                const auto cone = coneOf(m->flops[di]);
                for (const auto& source : cone->flops)
                    if (g.srcBits.contains(source.q))
                        capturesOf[{source.q, g.dstDomain}].emplace(di, &g);
            }
        }
        std::set<std::vector<std::string>> reported;
        for (auto& [launch, captures] : capturesOf) {
            if (captures.size() < 2) continue;
            const Group& g = *captures.begin()->second;
            const std::string launchName = m->nameOf(launch.first);
            std::vector<std::string> names{launchName};
            for (const auto& capture : captures) names.push_back(flopName(m->flops[capture.first]));
            std::sort(names.begin() + 1, names.end());
            if (!reported.insert(names).second) continue;
            const Relation rel = relation(g.srcDomain, g.dstDomain);
            Diagnostic d = mk("CDC-047", rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              launchName + " is synchronised " + std::to_string(captures.size()) + " times into clock domain '" + m->domains[g.dstDomain].name +
                              "': the copies can differ for a cycle, so logic that combines them can see a value that never existed" + withTag(g),
                              g.loc, g.hier, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Heuristic);
            ev(d, g);
            for (size_t i = 1; i < names.size(); i++) d.signals.push_back(names[i]);
            for (const auto& capture : captures) {
                const Flop& stage = m->flops[capture.first];
                d.evidence.push_back({"capture_chain", flopName(stage), procOf(stage).loc});
            }
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Synchronise once and fan out after the last stage, or show that no logic combines the copies.";
            add(std::move(d));
        }
    }

    auto dependsOnStage = [&](NodeId flopQ, NodeId targetQ) -> std::optional<bool> {
        auto start = m->flopOfQ.find(flopQ);
        if (start == m->flopOfQ.end()) return false;
        const uint32_t domain = m->flops[start->second].domain;
        std::vector<NodeId> pending{flopQ};
        std::unordered_set<NodeId> visited;
        while (!pending.empty() && visited.size() <= 256) {
            const NodeId q = pending.back(); pending.pop_back();
            if (!visited.insert(q).second) continue;
            const auto it = m->flopOfQ.find(q);
            if (it == m->flopOfQ.end()) continue;
            const auto heldCone = coneOf(m->flops[it->second]);
            for (const auto& source : heldCone->flops) {
                if (source.q == targetQ) return true;
                const auto sourceFlop = m->flopOfQ.find(source.q);
                if (sourceFlop != m->flopOfQ.end() && m->flops[sourceFlop->second].clockResolved &&
                    m->flops[sourceFlop->second].domain == domain && !visited.count(source.q))
                    pending.push_back(source.q);
            }
        }
        if (!pending.empty()) return std::nullopt;
        return false;
    };
    struct Coupling { bool any = false; bool complete = false; bool limited = false; };
    auto coupling = [&](const Group& source, const Group& destination) {
        std::vector<NodeId> firstStages;
        for (auto di : destination.dstFlops) {
            if (!haveChain_[di]) { chains_[di] = chainFor(di); haveChain_[di] = 1; }
            if (!chains_[di].stages.empty()) firstStages.push_back(m->flops[chains_[di].stages.front()].q);
        }
        std::sort(firstStages.begin(), firstStages.end());
        firstStages.erase(std::unique(firstStages.begin(), firstStages.end()), firstStages.end());
        if (source.srcBits.empty() || firstStages.empty()) return Coupling{};
        std::set<NodeId> reachedStages;
        bool everySourceBit = true;
        bool any = false;
        bool limited = false;
        for (NodeId sourceBit : source.srcBits) {
            bool reached = false;
            for (NodeId stage : firstStages) {
                const auto dependency = dependsOnStage(sourceBit, stage);
                limited |= !dependency.has_value();
                if (!dependency.value_or(false)) continue;
                reached = true; any = true; reachedStages.insert(stage);
            }
            everySourceBit &= reached;
        }
        return Coupling{any, everySourceBit && reachedStages.size() == firstStages.size(), limited};
    };
    struct FifoCandidate { Group* a; Group* b; Coupling ab; Coupling ba; };
    std::vector<FifoCandidate> fifoCandidates;
    // Pairs of gray groups in key order (the map's order), first key lower: only gray groups can pair.
    std::vector<Group*> grayGroups;
    for (auto& [key, g] : groups_) if (g.gray) grayGroups.push_back(&g);
    for (size_t i = 0; i < grayGroups.size(); ++i)
        for (size_t j = i + 1; j < grayGroups.size(); ++j) {
            Group &g1 = *grayGroups[i], &g2 = *grayGroups[j];
            if (g1.srcDomain != g2.dstDomain || g1.dstDomain != g2.srcDomain) continue;
            const Coupling ab = coupling(g1, g2), ba = coupling(g2, g1);
            if (ab.any || ba.any || ab.limited || ba.limited) fifoCandidates.push_back({&g1, &g2, ab, ba});
        }
    std::unordered_map<const Group*, size_t> possiblePartners;
    for (const auto& candidate : fifoCandidates) {
        possiblePartners[candidate.a]++;
        possiblePartners[candidate.b]++;
    }
    struct PointerResetCheck { bool missing = false; bool unresolved = false; };
    auto pointerReset = [&](const Group& group) {
        PointerResetCheck result;
        auto check = [&](const Flop& flop) {
            bool hasEffectiveReset = false;
            bool hasUncertainReset = false;
            for (size_t ri = 0; ri < flop.resets.size(); ++ri) {
                const char rv = ri < flop.resetValues.size() ? flop.resetValues[ri] : '?';
                if (rv == '0' || rv == '1') hasEffectiveReset = true;
                else if (rv != 'n') hasUncertainReset = true;
            }
            if (!hasEffectiveReset) {
                if (hasUncertainReset) result.unresolved = true;
                else result.missing = true;
            }
            for (const auto& reset : asyncResetSignature(flop)) {
                const NodeId node = reset.first;
                if (node == kNoNode) { result.unresolved = true; continue; }
                const RootKind kind = m->traceRoot(node).kind;
                result.unresolved |= kind == RootKind::Undriven || kind == RootKind::BlackBox ||
                    kind == RootKind::Cycle || m->constDrivers.contains(node) ||
                    (node < m->unresolvedDriven.size() && m->unresolvedDriven[node]);
            }
        };
        for (NodeId bit : group.srcBits) {
            const auto* flop = m->flopAt(bit);
            if (!flop) continue;
            check(*flop);
            const auto cone = coneOf(*flop);
            for (const auto& source : cone->flops)
                if (const auto* reg = m->flopAt(source.q)) check(*reg);
        }
        return result;
    };
    for (const auto& candidate : fifoCandidates) {
        Group& g1 = *candidate.a;
        Group& g2 = *candidate.b;
        const bool paired = candidate.ab.complete && candidate.ba.complete &&
                            possiblePartners[candidate.a] == 1 && possiblePartners[candidate.b] == 1;
        const PointerResetCheck reset1 = pointerReset(g1), reset2 = pointerReset(g2);
        if (reset1.unresolved || reset2.unresolved)
            incomplete("reset-extraction", "unresolved asynchronous reset source for FIFO pointers " +
                       g1.srcName + " / " + g2.srcName);
        if (!paired) {
            evaluated["CDC-034"]++;
            for (const auto& [pointer, check] : {std::pair{&g1, reset1}, std::pair{&g2, reset2}}) {
                if (!check.missing && !check.unresolved) continue;
                auto reset = mk("CDC-034", Severity::Warning, "possible FIFO pointer " + pointer->srcName +
                                (check.missing ? " has a register with no asynchronous reset"
                                               : " has a register with an unresolved asynchronous reset source") +
                                "; pointer pairing is unresolved",
                                pointer->loc, pointer->hier, Confidence::Unresolved);
                reset.signals = {pointer->srcName};
                reset.identityKey = pointer->srcName + ":unresolved-pointer-reset";
                reset.proofState = ProofState::NotAttempted;
                add(std::move(reset));
            }
            const std::string why = candidate.ab.limited || candidate.ba.limited
                ? "the bounded same-domain pointer dependency search did not finish"
                : candidate.ab.any && candidate.ba.any
                ? "pointer dependencies overlap or cover only part of the opposite synchronizer buses"
                : "only one direction of pointer dependency is established";
            Diagnostic o = mk("CDC-024", Severity::Warning,
                              "possible asynchronous FIFO coupling between " + g1.srcName + " / " + g2.srcName +
                              " is unresolved: " + why + "; pointer pairing and functional correctness are not established",
                              g1.loc, g1.hier, Confidence::Unresolved);
            o.signals = {g1.srcName, g2.srcName};
            o.identityKey = g1.srcName + ":" + g2.srcName + ":unresolved-coupling";
            o.proofState = ProofState::NotAttempted;
            o.evidence.push_back({"proof_obligation", "establish complete, unambiguous all-bit coupling and prove pointer pairing, arithmetic, full/empty and data-path behavior", {}});
            o.nextStep = "Review the complete pointer-bit dependencies and FIFO protocol; no pointer pair is inferred from partial or shared control flow.";
            add(std::move(o));
            Diagnostic r = mk("RDC-023", Severity::Warning,
                              "possible FIFO coupling between " + g1.srcName + " / " + g2.srcName +
                              " is unresolved: reset-release sequencing cannot be assigned to a proven pointer pair",
                              g1.loc, g1.hier, Confidence::Unresolved);
            r.signals = {g1.srcName, g2.srcName};
            r.identityKey = g1.srcName + ":" + g2.srcName + ":unresolved-reset-coupling";
            r.proofState = ProofState::NotAttempted;
            r.evidence.push_back({"proof_obligation", "establish the FIFO pointer pair and show reset release coordination before treating either side as valid", {}});
            add(std::move(r));
            evaluated["CDC-024"]++;
            evaluated["RDC-023"]++;
            incomplete("fifo-coupling", why + " for " + g1.srcName + " / " + g2.srcName);
            continue;
        }
        evaluated["CDC-034"]++; evaluated["CDC-024"]++;
            const bool missing = reset1.missing || reset2.missing;
            const bool unresolved = reset1.unresolved || reset2.unresolved;
            std::string msg = "asynchronous FIFO pointers " + g1.srcName + " (" + m->domains[g1.srcDomain].name + ") <-> " + g2.srcName + " (" + m->domains[g2.srcDomain].name + "): ";
            if (missing) msg += "a pointer register has no asynchronous reset; ";
            if (unresolved) msg += "a pointer register has an unresolved asynchronous reset source; ";
            const bool bad = missing || unresolved;
            Diagnostic d = mk("CDC-034", bad ? Severity::Warning : Severity::Info, bad ? msg + "pointer values can be inconsistent after reset" : msg + "opposite-direction Gray crossing candidate; every observed pointer bit has an asynchronous reset, but semantic FIFO pairing is not proven",
                              g1.loc, g1.hier, unresolved ? Confidence::Unresolved : Confidence::Heuristic);
            if (unresolved) d.proofState = ProofState::NotAttempted;
            d.signals = {g1.srcName, g2.srcName};   // the unproved FIFO obligations are CDC-024 and RDC-023
            add(std::move(d));
            Diagnostic o = mk("CDC-024", Severity::Warning, "functional correctness of the asynchronous FIFO candidate " + g1.srcName + " / " + g2.srcName + " (pointer pairing, arithmetic, full/empty, data path) is not verified",
                              g1.loc, g1.hier, Confidence::Heuristic);
            o.identityKey = g1.srcName + ":" + g2.srcName;
            o.proofState = ProofState::NotAttempted;
            add(std::move(o));
        evaluated["RDC-023"]++;
        auto resetsOf = [&](const Group& g) { std::set<NodeId> r; for (auto q : g.srcBits) if (auto* f = m->flopAt(q)) for (auto& rr : f->resets) r.insert(rr.root.node); return r; };
        std::set<NodeId> ra = resetsOf(g1), rb = resetsOf(g2);
        bool sameReset = !ra.empty() && ra == rb;
        Diagnostic resetFinding = mk("RDC-023", sameReset ? Severity::Info : Severity::Warning, "FIFO " + g1.srcName + " / " + g2.srcName + ": the order in which the two clock domains leave reset is not defined by the design; "
                          "a side that leaves reset early can read or write before the other side's pointers are valid" +
                          std::string(sameReset ? " (both pointers share one reset, but each domain still releases it on its own clock)" : " (the pointers are reset independently)"),
                          g1.loc, g1.hier, Confidence::Unresolved);
        resetFinding.signals = {g1.srcName, g2.srcName};
        resetFinding.proofState = ProofState::NotAttempted;
        resetFinding.evidence.push_back({"proof_obligation", "reset release of both domains must be sequenced (or each side must ignore the other until a synchronised ready); not verified", {}});
        add(std::move(resetFinding));
        if (!sameReset && !cfg->disabledRules.count("RDC-023")) {
            for (const Group* leg : {&g1, &g2}) {
                const bool cleanReset = !leg->dstFlops.empty() &&
                    std::none_of(leg->dstFlops.begin(), leg->dstFlops.end(), [&](uint32_t di) { return chains_[di].resetMismatch; });
                if (cleanReset) rdc023CoveredLegs.insert(leg);
            }
        }
    }
    for (const auto& [a, b] : recognizedHandshakes) {
            evaluated["RDC-023"]++;
            Diagnostic d = mk("RDC-023", Severity::Warning, "handshake " + a->srcName + " <-> " + b->srcName + ": the reset release order of its two domains is not defined; a request issued "
                              "right after one side leaves reset can be lost or acknowledged spuriously", a->loc, a->hier, Confidence::Unresolved);
            d.signals = {a->srcName, b->srcName};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
            if (!cfg->disabledRules.count("RDC-023")) {
                for (const Group* leg : {a, b}) {
                    const bool cleanReset = !leg->dstFlops.empty() &&
                        std::none_of(leg->dstFlops.begin(), leg->dstFlops.end(), [&](uint32_t di) { return chains_[di].resetMismatch; });
                    if (cleanReset) rdc023CoveredLegs.insert(leg);
                }
            }
        }
    for (auto& [group, finding] : pending041)
        if (!rdc023CoveredLegs.contains(group)) add(std::move(finding));
    // RDC-024: a synchronised control used as an asynchronous reset (aggregate across all stages sharing one synchroniser head)
    struct ResetHeadInfo {
        const SyncOut* rep = nullptr;
        std::set<uint32_t> userDomains;
    };
    std::map<uint32_t, ResetHeadInfo> resetHeads;
    for (auto& [q, so] : syncedOut_) {
        bool asReset = false;
        std::set<uint32_t> stageDomains;
        std::vector<NodeId> cl{q};
        std::unordered_set<NodeId> seen{q};
        for (size_t i = 0; i < cl.size(); i++)
            for (auto ei : m->out[cl[i]]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::AsyncCtl) {
                    asReset = true;
                    if (const auto* uf = m->flopAt(e.to); uf && uf->clockResolved)
                        stageDomains.insert(uf->domain);
                } else if (e.type == EdgeType::Comb && seen.insert(e.to).second) cl.push_back(e.to);
            }
        if (!asReset) continue;
        auto& info = resetHeads[so.head];
        if (!info.rep) info.rep = &so;
        info.userDomains.insert(stageDomains.begin(), stageDomains.end());
    }
    for (const auto& [headId, info] : resetHeads) {
        const SyncOut& so = *info.rep;
        evaluated["RDC-024"]++;
        const std::string launch = so.sourceIncomplete ? " (launch clock unresolved)" : " (domain " + m->domains[so.srcDomain].name + ")";
        std::string usedWhere = "and used there as an asynchronous reset";
        if (!info.userDomains.empty() && (info.userDomains.size() > 1 || *info.userDomains.begin() != so.dstDomain)) {
            std::string doms;
            for (auto ud : info.userDomains) {
                if (!doms.empty()) doms += ", ";
                doms += m->domains[ud].name;
            }
            usedWhere = "and used as an asynchronous reset in domain(s) [" + doms + "]";
        }
        Diagnostic d = mk("RDC-024", Severity::Warning, "control " + so.srcName + launch + " is synchronised into domain " + m->domains[so.dstDomain].name +
                          " " + usedWhere + ": a reset request/acknowledge protocol across domains; sequencing and completion are not verified", {}, so.hier,
                          so.sourceIncomplete ? Confidence::Unresolved : Confidence::Heuristic);
        d.signals = {so.srcName};
        d.proofState = ProofState::NotAttempted;
        d.evidence.push_back({"proof_obligation", "the requester must hold the request until the acknowledge from the reset domain returns; the reset must not release before its users are quiescent", {}});
        add(std::move(d));
    }

    // CDC-032: the bits of one source bus captured into separate registers must see the same synchroniser structure
    {
        struct Bus {
            std::set<int> depths;
            std::set<NodeId> sourceBits;
            std::set<NodeId> captures;
            const Group* first = nullptr;
        };
        std::map<std::tuple<std::string, uint32_t, uint32_t>, Bus> buses;
        for (auto& [key, g] : groups_) {
            if (g.collapsed) continue;
            auto& b = buses[{stripBit(g.srcName), g.srcDomain, g.dstDomain}];
            if (!b.first) b.first = &g;
            if (!g.sourceCaptures.empty()) {
                for (const auto& [source, capture] : g.sourceCaptures) {
                    auto it = m->flopOfQ.find(capture);
                    if (it == m->flopOfQ.end()) continue;
                    b.sourceBits.insert(source);
                    b.captures.insert(capture);
                    b.depths.insert(chains_[it->second].linkLogic ? 1 : chains_[it->second].depth);
                }
            } else {
                for (auto di : g.dstFlops) {
                    b.sourceBits.insert(g.srcQ);
                    b.captures.insert(m->flops[di].q);
                    b.depths.insert(chains_[di].linkLogic ? 1 : chains_[di].depth);
                }
            }
        }
        // bits of one source bus (or one destination vector) synchronised through separate registers: independent synchronisation
        std::set<std::tuple<std::string, uint32_t, uint32_t>> busDone;
        auto busWarn = [&](const Group& g, const std::string& what, uint64_t bits) {
            Relation rel = relation(g.srcDomain, g.dstDomain);
            if (rel == Relation::Same || g.gray || g.qualified) return;
            if (!busDone.emplace(what, g.srcDomain, g.dstDomain).second) return;
            evaluated["CDC-009"] += bits;
            Diagnostic d = mk("CDC-009", rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              std::to_string(bits) + " synchroniser captures of " + what + " are independent (separate registers/instances): skew can capture an inconsistent value",
                              g.loc, g.hier, rel == Relation::Unknown ? Confidence::Unresolved :
                              rel == Relation::Exclusive ? Confidence::Informational : Confidence::Established);
            d.signals = {g.srcName, g.dstName};
            if (rel == Relation::Exclusive)
                d.evidence.push_back({"trust_model", "the launch and capture clocks are declared exclusive by constraint (trusted, not verified)", {}});
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Prove the bus is Gray-coded / one-hot / held stable by a synchronised qualifier, or use a handshake or async FIFO.";
            add(std::move(d));
        };
        {
            struct SplitCapture {
                std::set<uint32_t> flops;
                const Group* first = nullptr;
                size_t sources = 0;
            };
            struct SplitSource {
                std::set<NodeId> bits;
                const Group* first = nullptr;
            };
            using BusKey = std::tuple<std::string, uint32_t, uint32_t>;
            std::map<BusKey, SplitCapture> byDst;
            std::map<BusKey, SplitSource> bySrc, grayLanes;
            for (const auto& [key, g] : groups_) {
                if (g.collapsed || g.unresolvedLanes) continue;
                const bool allSynced = std::all_of(g.dstFlops.begin(), g.dstFlops.end(), [&](uint32_t di) {
                    const auto& chain = chains_[di];
                    return !chain.callEffectsUnknown && !chain.linkLogic && !chain.feedbackWithFanout && chain.depth >= 2;
                });
                if (!allSynced) continue;
                if (m->symbolWidth(m->flops[g.dstFlops.front()].q) > 1) {
                    auto& capture = byDst[{g.dstName, g.srcDomain, g.dstDomain}];
                    capture.flops.insert(g.dstFlops.begin(), g.dstFlops.end());
                    if (!capture.first) capture.first = &g;
                    ++capture.sources;
                }
                if (g.dstFlops.size() != 1 || g.counter || g.qualified ||
                    g.anyLogicBefore || g.sourceCaptures.size() != 1) continue;
                auto& lane = (g.gray ? grayLanes : bySrc)[{g.srcName, g.srcDomain, g.dstDomain}];
                lane.bits.insert(g.srcQ);
                if (!lane.first) lane.first = &g;
            }
            for (const auto& [key, capture] : byDst)
                if (capture.sources >= 2 && capture.flops.size() >= 2)
                    busWarn(*capture.first, std::get<0>(key), capture.flops.size());
            for (const auto& [key, source] : bySrc)
                if (source.bits.size() >= 2)
                    busWarn(*source.first, std::get<0>(key), source.bits.size());
            for (const auto& [key, lanes] : grayLanes) {
                if (lanes.bits.size() < 2) continue;
                const Group& g = *lanes.first;
                Diagnostic d = mk("CDC-009", Severity::Warning,
                    "Gray pointer " + g.srcName + " reaches separate scalar synchronisers without a combined timing check",
                    g.loc, g.hier, Confidence::Unresolved);
                d.signals = {g.srcName};
                d.crossing = crossingOf(g);
                d.crossing->widthBits = lanes.bits.size();
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "verify the Gray property and a complete max-delay bound across the scalar lanes", g.loc});
                evaluated["CDC-009"] += lanes.bits.size();
                add(std::move(d));
                incomplete("cdc-017", "Gray pointer " + g.srcName + " has scalar lanes without a combined timing check");
            }
        }
        for (auto& [k, b] : buses) {
            if (b.captures.size() < 2 || b.depths.size() < 2) continue;
            const Group& g = *b.first;
            if (g.dstFlops.size() > 1) continue;   // a single destination vector is judged inside the crossing analysis
            evaluated["CDC-032"] += b.captures.size();
            Relation rel = relation(g.srcDomain, g.dstDomain);
            std::string ds;
            for (int d : b.depths) ds += (ds.empty() ? "" : "/") + std::to_string(d);
            Diagnostic d = mk("CDC-032", rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              (b.sourceBits.size() > 1 ? std::to_string(b.sourceBits.size()) + " distinct bits of " + stripBit(g.srcName) + " have different synchroniser depths" :
                               std::to_string(b.captures.size()) + " captures of " + g.srcName + " have different synchroniser depths") +
                              " across separate chains (" + ds + " stages): their values may not be coherent in the destination domain",
                              g.loc, g.hier, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
            d.signals = {g.srcName};
            d.nextStep = "Give every bit of a bundle the same synchroniser structure, or cross it with a qualifier/handshake.";
            add(std::move(d));
        }
    }

    // CDC-037: distinct clock roots that the constraints declare to be one clock (same SDC name): never proven equivalent
    for (auto& [a, b] : sameNameDomainPairs_) {
        evaluated["CDC-037"]++;
        Diagnostic d = mk("CDC-037", Severity::Warning, "clock roots '" + m->domains[a].name + "' and '" + m->domains[b].name + "' are declared as the same SDC clock '" + domainClockName(a) +
                          "': distinct roots are not proven equivalent; crossings between them remain unresolved",
                          {}, {}, Confidence::Unresolved);
        d.nextStep = "Use distinct clock names unless the two ports really carry the same clock; declare their relation with set_clock_groups.";
        add(std::move(d));
        incomplete("clock-relationships", "distinct clock roots share one SDC clock name: " + m->domains[a].name + ", " + m->domains[b].name);
    }

    // CDC-040: library cells declared as synchronisers (--library)
    for (auto& li : m->libInstances) {
        if (li.kind != "synchronizer") continue;   // flop, clock_gate, ram and blackbox cells are described by their own rules
        evaluated["CDC-040"]++;
        Diagnostic d = mk("CDC-040", Severity::Info, "library synchroniser cell '" + li.module + "' (" + std::to_string(li.stages) + " stages) at " + li.hierarchy +
                          " is recognised through the library description, not by its body", li.loc, li.hierarchy, Confidence::Heuristic);
        d.proofState = ProofState::NotAttempted;
        d.evidence.push_back({"trust_model", "the library declaration is trusted: the cell's real stage count, clocking and metastability behaviour are not verified", {}});
        add(std::move(d));
    }

    // CDC-040: modules the user annotated as synchronisers must contain a recognised chain
    if (design_ && design_->compilation) {
        AnnotatedFinder af;
        design_->compilation->getRoot().visit(af);
        for (auto& [path, def] : af.found) {
            evaluated["CDC-040"]++;
            bool recognised = false;
            for (const auto& [q, so] : syncedOut_)
                if (!so.stageMismatch && !so.dualEdgeStage && (so.hier == path || so.hier.rfind(path + ".", 0) == 0))
                    recognised = true;
            Diagnostic d = mk("CDC-040", recognised ? Severity::Info : Severity::Warning,
                              recognised ? "user-defined synchroniser module '" + def + "' (" + path + ") is annotated and a synchroniser structure was recognised inside it"
                                         : "module '" + def + "' (" + path + ") is annotated as a synchroniser but no synchroniser structure (>= 2 same-clock register stages fed from another domain) was recognised: "
                                           "the annotation contradicts the structure",
                              {}, path, recognised ? Confidence::Heuristic : Confidence::Established);
            if (recognised) {
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"trust_model", "the annotation and modeled chain describe structure; metastability, pulse delivery, and physical timing are unverified", {}});
            }
            add(std::move(d));
        }
    }
}

} // namespace sverity::cdc
