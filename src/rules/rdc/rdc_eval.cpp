#include "rules/rdc/rdc_eval.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/numeric/SVInt.h"

namespace sverity::rdc {
using namespace model;
using namespace slang::ast;
using Vec = std::vector<Tri>;

namespace {
constexpr size_t kMaxWidth = 256;

Tri mk(uint8_t v, bool t = false) { return Tri{v, t}; }
Tri notOf(Tri a) { return a.known() ? mk(a.v ^ 1) : mk(2, a.taint); }
Tri andOf(Tri a, Tri b) {
    if ((a.known() && a.v == 0) || (b.known() && b.v == 0)) return mk(0);
    if (a.known() && b.known()) return mk(1);
    return mk(2, a.taint || b.taint);
}
Tri orOf(Tri a, Tri b) {
    if ((a.known() && a.v == 1) || (b.known() && b.v == 1)) return mk(1);
    if (a.known() && b.known()) return mk(0);
    return mk(2, a.taint || b.taint);
}
Tri xorOf(Tri a, Tri b) {
    if (a.known() && b.known()) return mk(a.v ^ b.v);
    return mk(2, a.taint || b.taint);
}
// two values that may differ: equal and known stays, anything else is X, tainted when either side or the decision is
Tri mergeOf(Tri a, Tri b, bool decisionTaint) {
    if (a.known() && b.known() && a.v == b.v) return mk(a.v);
    return mk(2, decisionTaint || a.taint || b.taint);
}
bool allKnown(const Vec& v) {
    for (auto t : v) if (!t.known()) return false;
    return true;
}
bool anyTaint(const Vec& v) {
    for (auto t : v) if (t.taint) return true;
    return false;
}
uint64_t toInt(const Vec& v) {
    uint64_t r = 0;
    for (size_t i = 0; i < v.size() && i < 64; ++i) r |= uint64_t{v[i].v} << i;
    return r;
}
Vec fromInt(uint64_t x, size_t width) {
    Vec v(width);
    for (size_t i = 0; i < width; ++i) v[i] = mk(i < 64 ? (x >> i) & 1 : 0);
    return v;
}
Vec resized(Vec v, size_t width, bool sign) {
    const Tri fill = sign && !v.empty() ? v.back() : mk(0);
    v.resize(width, fill);
    return v;
}
// A select whose elements are wider than one bit indexes elements, not bits: not interpreted.
bool wideElements(const Type* t) {
    if (!t) return true;
    const Type* el = t->getArrayElementType();
    return el && el->getBitWidth() != 1;   // a plain vector or predefined integer has no element type: bit indexed
}
Tri reduceOr(const Vec& v) {
    Tri r = mk(0);
    for (auto t : v) r = orOf(r, t);
    return r;
}
Tri reduceAnd(const Vec& v) {
    Tri r = mk(1);
    for (auto t : v) r = andOf(r, t);
    return r;
}
Tri reduceXor(const Vec& v) {
    Tri r = mk(0);
    for (auto t : v) r = xorOf(r, t);
    return r;
}
} // namespace

struct EvalRun {
    ReleaseEvaluator& ev;
    const ProcInfo& proc;
    const ReleaseEvaluator::Lookup& env;
    bool ok = true;
    struct State {
        std::map<const ValueSymbol*, Vec> blk, nba;
    } st;

    Vec unknown(size_t width, bool taint = true) { return Vec(width, mk(2, taint)); }
    Vec fail(size_t width) { ok = false; return unknown(width); }

    // The bits of a symbol as the model holds them.
    Vec readSymbol(const ValueSymbol& sym, const State& s, bool nbaBase) {
        if (const auto it = s.blk.find(&sym); it != s.blk.end()) return it->second;
        if (nbaBase) {
            if (const auto it = s.nba.find(&sym); it != s.nba.end()) return it->second;
        }
        const auto width = sym.getType().isIntegral() ? sym.getType().getBitWidth() : 0;
        if (width == 0 || width > kMaxWidth) return fail(1);
        const auto it = ev.index_.find({proc.inst, &sym});
        Vec out(width, mk(2, true));
        if (it != ev.index_.end()) {
            const auto& nodes = it->second;
            for (size_t b = 0; b < width; ++b) {
                const NodeId n = nodes.size() == 1 && ev.m_.collapsed(nodes[0]) ? kNoNode : (b < nodes.size() ? nodes[b] : kNoNode);
                if (n != kNoNode) out[b] = env(n);
            }
        }
        return out;
    }

    Vec eval(const Expression& e, const State& s) {
        const size_t width = e.type && e.type->isIntegral() ? e.type->getBitWidth() : 0;
        if (width == 0 || width > kMaxWidth) return fail(1);
        if (const auto* c = e.getConstant(); c && c->isInteger()) {
            const auto& iv = c->integer();
            Vec v(width, mk(0));
            for (size_t b = 0; b < width && b < iv.getBitWidth(); ++b) {
                const auto bit = iv[static_cast<int32_t>(b)];
                v[b] = bit.isUnknown() ? mk(2) : mk(bit.value != 0 ? 1 : 0);
            }
            return v;
        }
        switch (e.kind) {
            case ExpressionKind::NamedValue: {
                const auto* sym = e.getSymbolReference();
                if (!sym || !sym->isValue()) return fail(width);
                Vec v = readSymbol(sym->as<ValueSymbol>(), s, false);
                return resized(std::move(v), width, false);
            }
            case ExpressionKind::Conversion: {
                const auto& op = e.as<ConversionExpression>().operand();
                if (!op.type || !op.type->isIntegral()) return fail(width);
                return resized(eval(op, s), width, op.type->isSigned());
            }
            case ExpressionKind::UnaryOp: {
                const auto& u = e.as<UnaryExpression>();
                Vec a = eval(u.operand(), s);
                switch (u.op) {
                    case UnaryOperator::BitwiseNot: { for (auto& t : a) t = notOf(t); return resized(a, width, false); }
                    case UnaryOperator::LogicalNot: return resized(Vec{notOf(reduceOr(a))}, width, false);
                    case UnaryOperator::BitwiseAnd: return resized(Vec{reduceAnd(a)}, width, false);
                    case UnaryOperator::BitwiseOr: return resized(Vec{reduceOr(a)}, width, false);
                    case UnaryOperator::BitwiseXor: return resized(Vec{reduceXor(a)}, width, false);
                    case UnaryOperator::BitwiseNand: return resized(Vec{notOf(reduceAnd(a))}, width, false);
                    case UnaryOperator::BitwiseNor: return resized(Vec{notOf(reduceOr(a))}, width, false);
                    case UnaryOperator::BitwiseXnor: return resized(Vec{notOf(reduceXor(a))}, width, false);
                    case UnaryOperator::Plus: return resized(a, width, false);
                    case UnaryOperator::Minus:
                        if (allKnown(a) && a.size() <= 64) return fromInt(~toInt(a) + 1, width);
                        return unknown(width, anyTaint(a));
                    default: return fail(width);
                }
            }
            case ExpressionKind::BinaryOp: {
                const auto& b = e.as<BinaryExpression>();
                Vec l = eval(b.left(), s), r = eval(b.right(), s);
                const bool lSigned = b.left().type && b.left().type->isSigned(), rSigned = b.right().type && b.right().type->isSigned();
                const auto bitwise = [&](Tri (*f)(Tri, Tri), bool invert) {
                    const size_t w = std::max({width, l.size(), r.size()});
                    l = resized(l, w, lSigned);
                    r = resized(r, w, rSigned);
                    Vec out(w);
                    for (size_t i = 0; i < w; ++i) out[i] = invert ? notOf(f(l[i], r[i])) : f(l[i], r[i]);
                    return resized(out, width, false);
                };
                const auto equal = [&]() {
                    const size_t w = std::max(l.size(), r.size());
                    l = resized(l, w, lSigned);
                    r = resized(r, w, rSigned);
                    Tri all = mk(1);
                    for (size_t i = 0; i < w; ++i) all = andOf(all, notOf(xorOf(l[i], r[i])));
                    return all;
                };
                switch (b.op) {
                    case BinaryOperator::BinaryAnd: return bitwise(andOf, false);
                    case BinaryOperator::BinaryOr: return bitwise(orOf, false);
                    case BinaryOperator::BinaryXor: return bitwise(xorOf, false);
                    case BinaryOperator::BinaryXnor: return bitwise(xorOf, true);
                    case BinaryOperator::LogicalAnd: return resized(Vec{andOf(reduceOr(l), reduceOr(r))}, width, false);
                    case BinaryOperator::LogicalOr: return resized(Vec{orOf(reduceOr(l), reduceOr(r))}, width, false);
                    case BinaryOperator::Equality: return resized(Vec{equal()}, width, false);
                    case BinaryOperator::Inequality: return resized(Vec{notOf(equal())}, width, false);
                    case BinaryOperator::Add: case BinaryOperator::Subtract: case BinaryOperator::LogicalShiftLeft:
                    case BinaryOperator::LogicalShiftRight: case BinaryOperator::GreaterThan: case BinaryOperator::GreaterThanEqual:
                    case BinaryOperator::LessThan: case BinaryOperator::LessThanEqual: {
                        if (!allKnown(l) || !allKnown(r) || l.size() > 64 || r.size() > 64 || lSigned || rSigned) return unknown(width, anyTaint(l) || anyTaint(r));
                        const uint64_t x = toInt(l), y = toInt(r);
                        uint64_t res = 0;
                        switch (b.op) {
                            case BinaryOperator::Add: res = x + y; break;
                            case BinaryOperator::Subtract: res = x - y; break;
                            case BinaryOperator::LogicalShiftLeft: res = y < 64 ? x << y : 0; break;
                            case BinaryOperator::LogicalShiftRight: res = y < 64 ? x >> y : 0; break;
                            case BinaryOperator::GreaterThan: res = x > y; break;
                            case BinaryOperator::GreaterThanEqual: res = x >= y; break;
                            case BinaryOperator::LessThan: res = x < y; break;
                            default: res = x <= y; break;
                        }
                        return fromInt(res, width);
                    }
                    default: return fail(width);
                }
            }
            case ExpressionKind::ConditionalOp: {
                const auto& c = e.as<ConditionalExpression>();
                if (c.conditions.size() != 1 || c.conditions[0].pattern) return fail(width);
                const Tri cond = reduceOr(eval(*c.conditions[0].expr, s));
                Vec a = eval(c.left(), s), b = eval(c.right(), s);
                a = resized(a, width, false);
                b = resized(b, width, false);
                if (cond.known()) return cond.v ? a : b;
                Vec out(width);
                for (size_t i = 0; i < width; ++i) out[i] = mergeOf(a[i], b[i], cond.taint);
                return out;
            }
            case ExpressionKind::Concatenation: {
                Vec out;
                const auto ops = e.as<ConcatenationExpression>().operands();
                for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
                    Vec part = eval(**it, s);
                    out.insert(out.end(), part.begin(), part.end());
                }
                return resized(out, width, false);
            }
            case ExpressionKind::Replication: {
                const auto& r = e.as<ReplicationExpression>();
                const auto* n = r.count().getConstant();
                if (!n || !n->isInteger() || n->integer().hasUnknown()) return fail(width);
                const Vec part = eval(r.concat(), s);
                Vec out;
                for (uint64_t i = 0, count = *n->integer().as<uint64_t>(); i < count && out.size() <= kMaxWidth; ++i) out.insert(out.end(), part.begin(), part.end());
                return resized(out, width, false);
            }
            case ExpressionKind::ElementSelect: {
                const auto& sel = e.as<ElementSelectExpression>();
                const auto* idx = sel.selector().getConstant();
                if (!idx || !idx->isInteger() || idx->integer().hasUnknown() || !sel.value().type || !sel.value().type->isIntegral() ||
                    wideElements(sel.value().type))
                    return fail(width);
                const auto range = sel.value().type->getFixedRange();
                const auto offset = range.containsPoint(idx->integer().as<int32_t>().value_or(0)) ? range.translateIndex(idx->integer().as<int32_t>().value_or(0)) : -1;
                const Vec v = eval(sel.value(), s);
                if (offset < 0 || static_cast<size_t>(offset) >= v.size()) return fail(width);
                return resized(Vec{v[static_cast<size_t>(offset)]}, width, false);
            }
            case ExpressionKind::RangeSelect: {
                const auto& sel = e.as<RangeSelectExpression>();
                const auto* l = sel.left().getConstant();
                const auto* r = sel.right().getConstant();
                if (sel.getSelectionKind() != RangeSelectionKind::Simple || !l || !r || !l->isInteger() || !r->isInteger() || l->integer().hasUnknown() ||
                    r->integer().hasUnknown() || !sel.value().type || !sel.value().type->isIntegral() || wideElements(sel.value().type))
                    return fail(width);
                const auto range = sel.value().type->getFixedRange();
                const int32_t li = l->integer().as<int32_t>().value_or(0), ri = r->integer().as<int32_t>().value_or(0);
                if (!range.containsPoint(li) || !range.containsPoint(ri)) return fail(width);
                const int32_t a = range.translateIndex(li), b = range.translateIndex(ri);
                const Vec v = eval(sel.value(), s);
                Vec out;
                for (int32_t i = std::min(a, b); i <= std::max(a, b); ++i) {
                    if (static_cast<size_t>(i) >= v.size()) return fail(width);
                    out.push_back(v[static_cast<size_t>(i)]);
                }
                return resized(out, width, false);
            }
            case ExpressionKind::IntegerLiteral: {
                const auto& iv = e.as<IntegerLiteral>().getValue();
                Vec v(width, mk(0));
                for (size_t b = 0; b < width && b < iv.getBitWidth(); ++b) {
                    const auto bit = iv[static_cast<int32_t>(b)];
                    v[b] = bit.isUnknown() ? mk(2) : mk(bit.value != 0 ? 1 : 0);
                }
                return v;
            }
            default: return fail(width);
        }
    }

    // Writes `value` into the bits of `lhs`.
    void assign(const Expression& lhs, const Vec& value, bool nonBlocking, State& s) {
        auto& target = nonBlocking ? s.nba : s.blk;
        if (lhs.kind == ExpressionKind::Concatenation) {
            size_t at = value.size();
            for (const auto* op : lhs.as<ConcatenationExpression>().operands()) {
                const size_t w = op->type && op->type->isIntegral() ? op->type->getBitWidth() : 0;
                if (w == 0 || w > at) { ok = false; return; }
                at -= w;
                assign(*op, Vec(value.begin() + static_cast<long>(at), value.begin() + static_cast<long>(at + w)), nonBlocking, s);
            }
            return;
        }
        const Expression* base = &lhs;
        size_t lo = 0, w = 0;
        if (lhs.kind == ExpressionKind::ElementSelect || lhs.kind == ExpressionKind::RangeSelect) {
            const Expression& inner = lhs.kind == ExpressionKind::ElementSelect ? lhs.as<ElementSelectExpression>().value() : lhs.as<RangeSelectExpression>().value();
            base = &inner;
            if (!inner.type || !inner.type->isIntegral() || wideElements(inner.type)) { ok = false; return; }
            const auto range = inner.type->getFixedRange();
            int32_t a = 0, b = 0;
            const auto cst = [&](const Expression& x, int32_t& out) {
                const auto* c = x.getConstant();
                if (!c || !c->isInteger() || c->integer().hasUnknown()) return false;
                out = c->integer().as<int32_t>().value_or(0);
                return range.containsPoint(out);
            };
            if (lhs.kind == ExpressionKind::ElementSelect) {
                if (!cst(lhs.as<ElementSelectExpression>().selector(), a)) { ok = false; return; }
                b = a;
            } else {
                const auto& rs = lhs.as<RangeSelectExpression>();
                if (rs.getSelectionKind() != RangeSelectionKind::Simple || !cst(rs.left(), a) || !cst(rs.right(), b)) { ok = false; return; }
            }
            a = range.translateIndex(a);
            b = range.translateIndex(b);
            lo = static_cast<size_t>(std::min(a, b));
            w = static_cast<size_t>(std::abs(a - b)) + 1;
        }
        if (base->kind != ExpressionKind::NamedValue) { ok = false; return; }
        const auto* sym = base->getSymbolReference();
        if (!sym || !sym->isValue()) { ok = false; return; }
        const auto& vs = sym->as<ValueSymbol>();
        if (!vs.getType().isIntegral() || vs.getType().getBitWidth() > kMaxWidth) { ok = false; return; }
        if (w == 0) w = vs.getType().getBitWidth();
        auto it = target.find(&vs);
        if (it == target.end()) it = target.emplace(&vs, readSymbol(vs, s, !nonBlocking ? false : false)).first;
        if (it->second.size() < lo + w) it->second.resize(lo + w, mk(2, true));
        const Vec fitted = resized(value, w, false);
        for (size_t i = 0; i < w; ++i) it->second[lo + i] = fitted[i];
    }

    State merge(const State& a, const State& b, bool decisionTaint) {
        State out;
        const auto mergeMaps = [&](const std::map<const ValueSymbol*, Vec>& x, const std::map<const ValueSymbol*, Vec>& y, bool nbaMap,
                                   std::map<const ValueSymbol*, Vec>& dst) {
            std::set<const ValueSymbol*> keys;
            for (const auto& [k, v] : x) keys.insert(k);
            for (const auto& [k, v] : y) keys.insert(k);
            for (const auto* k : keys) {
                const auto xi = x.find(k), yi = y.find(k);
                // a side that did not write keeps the pre-statement value
                const Vec xv = xi != x.end() ? xi->second : readSymbol(*k, nbaMap ? a : a, false);
                const Vec yv = yi != y.end() ? yi->second : readSymbol(*k, b, false);
                Vec m(std::max(xv.size(), yv.size()));
                for (size_t i = 0; i < m.size(); ++i)
                    m[i] = mergeOf(i < xv.size() ? xv[i] : mk(2, true), i < yv.size() ? yv[i] : mk(2, true), decisionTaint);
                dst[k] = m;
            }
        };
        mergeMaps(a.blk, b.blk, false, out.blk);
        mergeMaps(a.nba, b.nba, true, out.nba);
        return out;
    }

    void exec(const Statement& stmt, State& s) {
        switch (stmt.kind) {
            case StatementKind::Empty: return;
            case StatementKind::List:
                for (const auto* child : stmt.as<StatementList>().list) exec(*child, s);
                return;
            case StatementKind::Block: exec(stmt.as<BlockStatement>().body, s); return;
            case StatementKind::Timed: exec(stmt.as<TimedStatement>().stmt, s); return;
            case StatementKind::VariableDeclaration: return;
            case StatementKind::ExpressionStatement: {
                const auto& e = stmt.as<ExpressionStatement>().expr;
                if (e.kind != ExpressionKind::Assignment) { ok = false; return; }
                const auto& as = e.as<AssignmentExpression>();
                if (as.isCompound()) { ok = false; return; }
                Vec rhs = eval(as.right(), s);
                assign(as.left(), rhs, as.isNonBlocking(), s);
                return;
            }
            case StatementKind::Conditional: {
                const auto& c = stmt.as<ConditionalStatement>();
                if (c.conditions.size() != 1 || c.conditions[0].pattern) { ok = false; return; }
                const Tri cond = reduceOr(eval(*c.conditions[0].expr, s));
                if (cond.known()) {
                    if (cond.v) exec(c.ifTrue, s);
                    else if (c.ifFalse) exec(*c.ifFalse, s);
                    return;
                }
                State a = s, b = s;
                exec(c.ifTrue, a);
                if (c.ifFalse) exec(*c.ifFalse, b);
                s = merge(a, b, cond.taint);
                return;
            }
            case StatementKind::Case: {
                const auto& c = stmt.as<CaseStatement>();
                if (c.condition != CaseStatementCondition::Normal) { ok = false; return; }
                const Vec sel = eval(c.expr, s);
                execCase(c, sel, 0, s);
                return;
            }
            default: ok = false; return;
        }
    }

    void execCase(const CaseStatement& c, const Vec& sel, size_t arm, State& s) {
        if (arm >= c.items.size()) {
            if (c.defaultCase) exec(*c.defaultCase, s);
            return;
        }
        Tri match = mk(0);
        for (const auto* ex : c.items[arm].expressions) {
            Vec v = eval(*ex, s);
            const size_t w = std::max(v.size(), sel.size());
            Vec x = resized(v, w, false), y = resized(sel, w, false);
            Tri eq = mk(1);
            for (size_t i = 0; i < w; ++i) eq = andOf(eq, notOf(xorOf(x[i], y[i])));
            match = orOf(match, eq);
        }
        if (match.known()) {
            if (match.v) exec(*c.items[arm].stmt, s);
            else execCase(c, sel, arm + 1, s);
            return;
        }
        State a = s, b = s;
        exec(*c.items[arm].stmt, a);
        execCase(c, sel, arm + 1, b);
        s = merge(a, b, match.taint);
    }
};

void ReleaseEvaluator::buildIndex() {
    if (indexed_) return;
    indexed_ = true;
    for (NodeId n = 0; n < m_.nodes.size(); ++n) {
        const auto& node = m_.nodes[n];
        if (!node.sym) continue;
        auto& v = index_[{node.inst, node.sym}];
        if (v.size() <= node.bit) v.resize(node.bit + 1, kNoNode);
        v[node.bit] = n;
    }
}

Tri ReleaseEvaluator::nextValue(uint32_t fi, const Lookup& env, bool& ok) {
    buildIndex();
    const Flop& f = m_.flops[fi];
    const auto& proc = m_.procs[f.proc];
    ok = false;
    if (!proc.block || f.q >= m_.nodes.size() || !m_.nodes[f.q].sym || m_.collapsed(f.q)) return mk(2, true);
    const ValueSymbol* target = m_.nodes[f.q].sym;
    const size_t bit = m_.nodes[f.q].bit;
    EvalRun run{*this, proc, env};
    run.ok = true;
    run.exec(proc.block->getBody(), run.st);
    if (!run.ok) return mk(2, true);
    ok = true;
    // an assignment the procedure never makes keeps the register's own value
    Vec v;
    if (const auto it = run.st.nba.find(target); it != run.st.nba.end()) v = it->second;
    else if (const auto jt = run.st.blk.find(target); jt != run.st.blk.end()) v = jt->second;
    else v = run.readSymbol(*target, run.st, false);
    if (bit >= v.size()) { ok = false; return mk(2, true); }
    return v[bit];
}

} // namespace sverity::rdc
