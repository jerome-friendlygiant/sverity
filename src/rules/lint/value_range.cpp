#include "rules/lint/value_range.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>

#include "analysis/temps.h"
#include "domains/model.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "sverity/lint.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/numeric/SVInt.h"

namespace sverity::lint {

using namespace slang;
using namespace slang::ast;

std::string toString(Wide v) {
    if (v == 0) return "0";
    const bool negative = v < 0;
    unsigned __int128 u = negative ? -static_cast<unsigned __int128>(v) : static_cast<unsigned __int128>(v);
    std::string digits;
    while (u != 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(u % 10)));
        u /= 10;
    }
    return negative ? "-" + digits : digits;
}

std::string toString(const Interval& i) {
    return i.lo == i.hi ? toString(i.lo) : "[" + toString(i.lo) + ", " + toString(i.hi) + "]";
}

std::optional<Interval> typeRange(const Type& t) {
    if (!t.isIntegral()) return std::nullopt;
    const auto w = t.getBitWidth();
    if (w == 0 || w > 64) return std::nullopt;
    const Wide one = 1;
    if (t.isSigned()) return Interval{-(one << (w - 1)), (one << (w - 1)) - 1};
    return Interval{0, (one << w) - 1};
}

namespace {

// The values an `input` formal of a user function takes: the hull of what every call in the design passes. A function called with
// constants (`rotr(v, 6)`, `SecAddRandWidth(8)`) then has the range of those constants, not of its argument type. A formal the body
// writes, a function with a call that cannot be read (a call by reference, a task, a function reached by a hierarchical name or
// through a class) and a formal no call passes have no entry: their range is their type's.
struct FormalRanges {
    std::unordered_map<const ValueSymbol*, Interval> byFormal;
};

const FormalRanges& formalRangesOf(Lint& l) {
    // Kept on the design's model, so it lives and dies with the run's compilation (an address-keyed cache would outlive it).
    static std::mutex guard;
    std::lock_guard<std::mutex> lock(guard);
    auto& slot = model::getModel(l.context().design).formalRanges;
    if (slot) return *static_cast<const FormalRanges*>(slot.get());
    auto computed = std::make_shared<FormalRanges>();
    FormalRanges& out = *computed;
    slot = computed;
    struct Collect : HardwareVisitor<Collect, VisitFlags::AllGood> {
        Lint& l;
        ValueRanges plain;   // ranges of what a call passes, without formal ranges (a call inside a function passes its own formals: type range)
        std::unordered_map<const ValueSymbol*, Interval> hull;
        std::unordered_set<const ValueSymbol*> poisoned;
        std::unordered_set<const SubroutineSymbol*> scanned;
        explicit Collect(Lint& lint) : l(lint), plain(lint, false, false) {}
        static const Symbol* rootOf(const Expression& e) {
            const Expression* x = &e;
            for (;;) {
                switch (x->kind) {
                    case ExpressionKind::Conversion: x = &x->as<ConversionExpression>().operand(); break;
                    case ExpressionKind::ElementSelect: x = &x->as<ElementSelectExpression>().value(); break;
                    case ExpressionKind::RangeSelect: x = &x->as<RangeSelectExpression>().value(); break;
                    case ExpressionKind::MemberAccess: x = &x->as<MemberAccessExpression>().value(); break;
                    case ExpressionKind::NamedValue: return &x->as<NamedValueExpression>().symbol;
                    default: return nullptr;
                }
            }
        }
        void poisonTarget(const Expression& e) {
            if (e.kind == ExpressionKind::Concatenation) {
                for (const auto* op : e.as<ConcatenationExpression>().operands()) poisonTarget(*op);
                return;
            }
            if (const auto* sym = rootOf(e); sym && sym->kind == SymbolKind::FormalArgument) poisoned.insert(static_cast<const ValueSymbol*>(sym));
        }
        // Writes to a formal inside a body, found once per function.
        struct Writes : ASTVisitor<Writes, VisitFlags::AllGood> {
            Collect& c;
            explicit Writes(Collect& k) : c(k) {}
            void handle(const AssignmentExpression& a) { c.poisonTarget(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement ||
                    u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Postdecrement) c.poisonTarget(u.operand());
                visitDefault(u);
            }
        };
        void handle(const CallExpression& call) {
            visitDefault(call);
            if (call.isSystemCall()) return;
            const auto* sub = call.thisClass() ? nullptr : std::get_if<const SubroutineSymbol*>(&call.subroutine);
            if (!sub || !*sub) return;
            const SubroutineSymbol& callee = **sub;
            if (scanned.insert(&callee).second) {
                Writes w(*this);
                callee.getBody().visit(w);
                if (callee.subroutineKind != SubroutineKind::Function || callee.isVirtual() ||
                    callee.flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern))
                    for (const auto* formal : callee.getArguments()) poisoned.insert(formal);
            }
            const auto formals = callee.getArguments();
            const auto args = call.arguments();
            for (size_t i = 0; i < formals.size(); ++i) {
                const ValueSymbol* formal = formals[i];
                if (i >= args.size() || formals[i]->direction != ArgumentDirection::In) { poisoned.insert(formal); continue; }
                const auto range = plain.of(*args[i]);
                if (!range) { poisoned.insert(formal); continue; }
                auto [it, fresh] = hull.try_emplace(formal, *range);
                if (!fresh) { it->second.lo = std::min(it->second.lo, range->lo); it->second.hi = std::max(it->second.hi, range->hi); }
            }
        }
    } collect(l);
    l.visitHardware(collect);
    for (const auto* package : l.compilation().getPackages()) package->visit(collect);
    for (const auto& [formal, range] : collect.hull)
        if (!collect.poisoned.count(formal)) out.byFormal.emplace(formal, range);
    return out;
}

// The value of a constant integral expression, exactly; nothing for a non-constant or one with unknown bits or beyond 64 bits.
std::optional<Wide> constantWide(const Lint& l, const Expression& e) {
    const auto cv = l.evalConst(e);
    if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
    const SVInt& v = cv.integer();
    if (v.getBitWidth() <= 64) {
        if (v.isSigned()) {
            if (auto x = v.as<int64_t>()) return Wide(*x);
        }
        else if (auto x = v.as<uint64_t>())
            return Wide(*x);
        return std::nullopt;
    }
    if (v.getMinRepresentedBits() > 63) return std::nullopt;
    if (auto x = v.as<int64_t>()) return Wide(*x);
    return std::nullopt;
}

// Match only a signed variable's own declared sign bit, including ascending ranges.
const Expression* signExtendedBase(const Lint& l, const Type& to, const Expression& op) {
    if (!to.isSigned() || op.kind != ExpressionKind::Concatenation) return nullptr;
    const auto parts = op.as<ConcatenationExpression>().operands();
    if (parts.size() != 2) return nullptr;
    const auto& base = unwrapImplicit(*parts[1]);
    if (base.kind != ExpressionKind::NamedValue) return nullptr;
    const auto& sym = base.as<NamedValueExpression>().symbol;
    const auto& type = sym.getType().getCanonicalType();
    if (!type.isIntegral() || !type.isSigned() || !type.getBitWidth()) return nullptr;
    const Expression* top = &unwrapImplicit(*parts[0]);
    int64_t copies = 1;
    if (top->kind == ExpressionKind::Replication) {
        const auto& rep = top->as<ReplicationExpression>();
        const auto count = constantInt(l, rep.count());
        if (!count || *count < 1) return nullptr;
        copies = *count;
        top = &unwrapImplicit(rep.concat());
        if (top->kind == ExpressionKind::Concatenation && top->as<ConcatenationExpression>().operands().size() == 1)
            top = &unwrapImplicit(*top->as<ConcatenationExpression>().operands()[0]);
    }
    if (top->kind != ExpressionKind::ElementSelect) return nullptr;
    const auto& select = top->as<ElementSelectExpression>();
    const auto& selected = unwrapImplicit(select.value());
    if (selected.kind != ExpressionKind::NamedValue || &selected.as<NamedValueExpression>().symbol != &sym) return nullptr;
    const auto index = constantInt(l, select.selector());
    const int64_t msb = type.kind == SymbolKind::PackedArrayType ? type.getFixedRange().left : int64_t(type.getBitWidth()) - 1;
    if (!index || *index != msb || int64_t(to.getBitWidth()) != int64_t(type.getBitWidth()) + copies) return nullptr;
    return &base;
}

bool mulOverflows(Wide a, Wide b, Wide& out) { return __builtin_mul_overflow(a, b, &out); }

// Smallest all-ones value that covers v >= 0.
Wide coverMask(Wide v) {
    Wide m = 0;
    while (m < v) m = (m << 1) | 1;
    return m;
}

Interval hull(const std::vector<Wide>& values) {
    return {*std::min_element(values.begin(), values.end()), *std::max_element(values.begin(), values.end())};
}

bool isShift(BinaryOperator op) {
    return op == BinaryOperator::LogicalShiftLeft || op == BinaryOperator::ArithmeticShiftLeft ||
           op == BinaryOperator::LogicalShiftRight || op == BinaryOperator::ArithmeticShiftRight;
}

} // namespace

bool ValueRanges::enter(const ForLoopStatement& loop) {
    const auto values = constantLoopValues(loop, l_, allowWrap_);
    if (!values) return false;
    const auto type = typeRange(values->var->getType());
    if (!type) return false;
    LoopRange r;
    r.body = {values->min(), values->max()};
    // The value that ends the loop: the one after the last the body sees.
    const Wide first = values->first, ending = Wide(values->first) + Wide(values->count) * values->step;
    r.test = values->step > 0 ? Interval{first, ending} : Interval{ending, first};
    if (!r.test.within(*type)) r.test = *type;
    loops_[values->var] = r;
    if (values->wraps) wrapping_.insert(values->var);
    entered_[&loop] = values->var;
    return true;
}

void ValueRanges::leave(const ForLoopStatement& loop) {
    if (auto it = entered_.find(&loop); it != entered_.end()) {
        loops_.erase(it->second);
        wrapping_.erase(it->second);
        entered_.erase(it);
    }
}

void ValueRanges::testPhase(const ForLoopStatement& loop, bool on) {
    if (auto it = entered_.find(&loop); it != entered_.end()) loops_[it->second].inTest = on;
}

std::optional<Interval> ValueRanges::loopRange(const ValueSymbol* var) const {
    if (auto it = loops_.find(var); it != loops_.end()) return it->second.body;
    return std::nullopt;
}

std::optional<Interval> ValueRanges::narrow(const ValueSymbol* var, const Interval& allowed, bool& empty) {
    empty = false;
    auto it = loops_.find(var);
    if (it == loops_.end()) return std::nullopt;
    const Interval saved = it->second.body;
    const Interval cut{std::max(saved.lo, allowed.lo), std::min(saved.hi, allowed.hi)};
    if (cut.lo > cut.hi) empty = true;
    else it->second.body = cut;
    return saved;
}

void ValueRanges::restore(const ValueSymbol* var, const std::optional<Interval>& saved) {
    if (!saved) return;
    if (auto it = loops_.find(var); it != loops_.end()) it->second.body = *saved;
}

// The operand's values after it is converted to `to` the way the language does it: kept when they all fit, else anything.
std::optional<Interval> ValueRanges::valueIn(const Expression& operand, const Type& to) const {
    const auto in = of(operand);
    const auto out = typeRange(to);
    if (!in || !out) return std::nullopt;
    return in->within(*out) ? *in : *out;
}

std::optional<Interval> ValueRanges::binary(const BinaryExpression& b, bool clamp) const {
    const Type& t = *b.type;
    const auto full = typeRange(t);
    if (!full) return std::nullopt;
    const auto op = b.op;
    const bool arithmetic = op == BinaryOperator::Add || op == BinaryOperator::Subtract || op == BinaryOperator::Multiply ||
                            op == BinaryOperator::Divide || op == BinaryOperator::Mod || op == BinaryOperator::BinaryAnd ||
                            op == BinaryOperator::BinaryOr || op == BinaryOperator::BinaryXor;
    if (!arithmetic && !isShift(op)) return clamp ? full : std::nullopt;   // comparisons, logic: one bit, or unmodelled

    const auto l = valueIn(b.left(), t);
    // A shift count is self-determined; every other operand is converted to the result type.
    const auto r = isShift(op) ? of(b.right()) : valueIn(b.right(), t);
    std::optional<Interval> m;
    if (l && r) {
        switch (op) {
            case BinaryOperator::Add: m = Interval{l->lo + r->lo, l->hi + r->hi}; break;
            case BinaryOperator::Subtract: m = Interval{l->lo - r->hi, l->hi - r->lo}; break;
            case BinaryOperator::Multiply: {
                Wide a, c, d, e;
                if (!mulOverflows(l->lo, r->lo, a) && !mulOverflows(l->lo, r->hi, c) && !mulOverflows(l->hi, r->lo, d) &&
                    !mulOverflows(l->hi, r->hi, e))
                    m = hull({a, c, d, e});
                break;
            }
            case BinaryOperator::LogicalShiftLeft:
            case BinaryOperator::ArithmeticShiftLeft:
                if (r->lo >= 0 && r->hi <= 62) {
                    const Wide f = Wide(1) << static_cast<int>(r->lo), g = Wide(1) << static_cast<int>(r->hi);
                    m = hull({l->lo * f, l->lo * g, l->hi * f, l->hi * g});
                }
                break;
            case BinaryOperator::LogicalShiftRight:
                // Zero-fill: the value is the unsigned pattern, which is the value itself only when it is not negative.
                if (l->lo >= 0 && r->lo >= 0 && r->hi <= 126)
                    m = Interval{l->lo >> static_cast<int>(r->hi), l->hi >> static_cast<int>(r->lo)};
                break;
            case BinaryOperator::ArithmeticShiftRight:
                if (r->lo >= 0 && r->hi <= 126) {
                    const int a = static_cast<int>(r->lo), c = static_cast<int>(r->hi);
                    if (!t.isSigned()) {
                        if (l->lo >= 0) m = Interval{l->lo >> c, l->hi >> a};
                    }
                    else m = hull({l->lo >> a, l->lo >> c, l->hi >> a, l->hi >> c});
                }
                break;
            case BinaryOperator::BinaryAnd:
                // x & m with m >= 0 is between 0 and m, whatever x is.
                if (l->lo >= 0 && r->lo >= 0) m = Interval{0, std::min(l->hi, r->hi)};
                else if (l->lo >= 0) m = Interval{0, l->hi};
                else if (r->lo >= 0) m = Interval{0, r->hi};
                break;
            case BinaryOperator::BinaryOr:
                if (l->lo >= 0 && r->lo >= 0) m = Interval{std::max(l->lo, r->lo), coverMask(std::max(l->hi, r->hi))};
                break;
            case BinaryOperator::BinaryXor:
                if (l->lo >= 0 && r->lo >= 0) m = Interval{0, coverMask(std::max(l->hi, r->hi))};
                break;
            case BinaryOperator::Mod:
                // The remainder takes the sign of the dividend and is smaller than the divisor.
                if (r->lo > 0) m = Interval{l->lo >= 0 ? Wide(0) : -(r->hi - 1), l->hi <= 0 ? Wide(0) : std::min(l->hi, r->hi - 1)};
                break;
            case BinaryOperator::Divide:
                if (r->lo > 0) m = hull({l->lo / r->lo, l->lo / r->hi, l->hi / r->lo, l->hi / r->hi});
                break;
            default: break;
        }
    }
    if (!clamp) return m;
    if (!m || !m->within(*full)) return full;
    return m;
}

std::optional<Interval> ValueRanges::mathematical(const Expression& e) const {
    if (e.kind != ExpressionKind::BinaryOp || !e.type) return std::nullopt;
    return binary(e.as<BinaryExpression>(), false);
}

bool ValueRanges::wrapFree(const Expression& e) const {
    if (!e.type) return false;
    const auto m = mathematical(e);
    const auto full = typeRange(*e.type);
    return m && full && m->within(*full);
}

namespace {
struct WriteScan : ASTVisitor<WriteScan, VisitFlags::AllGood> {
    BlockingWrites out;
    std::unordered_set<const SubroutineSymbol*> scanned;
    void target(const Expression& lhs) {
        for (const auto* sym : lvalueSymbols(lhs)) out.symbols.insert(sym);
    }
    void handle(const AssignmentExpression& a) {
        if (!a.isNonBlocking()) target(a.left());
        visitDefault(a);
    }
    void handle(const UnaryExpression& u) {
        if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
            u.op == UnaryOperator::Postdecrement)
            target(u.operand());
        visitDefault(u);
    }
    void handle(const CallExpression& call) {
        visitDefault(call);
        if (call.isSystemCall()) {
            const auto name = call.getSubroutineName();
            if (name.find("scanf") != std::string_view::npos || name.find("plusargs") != std::string_view::npos ||
                name.find("read") != std::string_view::npos)
                out.unknown = true;
            return;
        }
        const auto* sub = call.thisClass() ? nullptr : std::get_if<const SubroutineSymbol*>(&call.subroutine);
        if (!sub || !*sub) {
            out.unknown = true;
            return;
        }
        const SubroutineSymbol& callee = **sub;
        if (callee.isVirtual() || callee.flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern)) {
            out.unknown = true;
            return;
        }
        const auto formals = callee.getArguments();
        const auto args = call.arguments();
        for (size_t i = 0; i < formals.size() && i < args.size(); ++i)
            if (formals[i]->direction != ArgumentDirection::In) target(*args[i]);
        if (scanned.insert(&callee).second) callee.getBody().visit(*this);
    }
};
} // namespace

BlockingWrites blockingWritesIn(const Statement& body) {
    WriteScan scan;
    body.visit(scan);
    return std::move(scan.out);
}

ValueRanges::Narrowing::Narrowing(ValueRanges& ranges, const std::vector<Guard>& guards) : r_(ranges) {
    for (const auto& g : guards) {
        const auto it = r_.guards_.find(g.var);
        std::optional<Interval> before;
        Interval now = g.range;
        if (it != r_.guards_.end()) {
            before = it->second;
            now = {std::max(now.lo, it->second.lo), std::min(now.hi, it->second.hi)};
            if (now.lo > now.hi) continue;   // contradicting guards: the branch never runs; keep what is already known
        }
        saved_.push_back({g.var, before});
        r_.guards_[g.var] = now;
    }
}

ValueRanges::Narrowing::~Narrowing() {
    for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
        if (it->second) r_.guards_[it->first] = *it->second;
        else r_.guards_.erase(it->first);
    }
}

std::vector<ValueRanges::Guard> ValueRanges::guardsOf(const Expression& cond, bool truth) const { return guardsOf(cond, truth, 0); }

// The expression a net or variable is continuously assigned, when that assignment is its only driver and covers all of it.
static const Expression* soleContinuousValue(Lint& l, const ValueSymbol& sym) {
    const auto* analysis = l.context().design.analysis.get();
    if (!analysis || (sym.kind != SymbolKind::Net && sym.kind != SymbolKind::Variable)) return nullptr;
    const auto drivers = analysis->getDrivers(sym);
    if (drivers.size() != 1 || drivers[0]->kind != slang::analysis::DriverKind::Continuous || drivers[0]->isInputPort()) return nullptr;
    const auto* assign = drivers[0]->containingSymbol->as_if<ContinuousAssignSymbol>();
    if (!assign || assign->getAssignment().kind != ExpressionKind::Assignment) return nullptr;
    const auto& a = assign->getAssignment().as<AssignmentExpression>();
    if (a.isNonBlocking() || a.timingControl || a.left().kind != ExpressionKind::NamedValue ||
        &a.left().as<NamedValueExpression>().symbol != &sym)
        return nullptr;
    return &a.right();
}

std::vector<ValueRanges::Guard> ValueRanges::guardsOf(const Expression& cond, bool truth, int depth) const {
    if (depth > 6 || !cond.type) return {};
    const Expression& e = unwrapImplicit(cond);
    if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot)
        return guardsOf(e.as<UnaryExpression>().operand(), !truth, depth + 1);
    if (e.kind == ExpressionKind::NamedValue) {
        const auto& sym = e.as<NamedValueExpression>().symbol;
        if (loops_.count(&sym) || !e.type->isIntegral() || e.type->getBitWidth() != 1) return {};
        const auto* value = soleContinuousValue(l_, sym);
        return value ? guardsOf(*value, truth, depth + 1) : std::vector<Guard>{};
    }
    if (e.kind != ExpressionKind::BinaryOp) return {};
    const auto& b = e.as<BinaryExpression>();
    if (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) {
        const bool both = (b.op == BinaryOperator::LogicalAnd) == truth;
        auto x = guardsOf(b.left(), truth, depth + 1), y = guardsOf(b.right(), truth, depth + 1);
        if (both) {
            x.insert(x.end(), y.begin(), y.end());
            return x;
        }
        // One of the two holds: only a variable both sides constrain keeps a bound, the hull of the two.
        std::vector<Guard> out;
        for (const auto& gx : x)
            for (const auto& gy : y)
                if (gx.var == gy.var) out.push_back({gx.var, {std::min(gx.range.lo, gy.range.lo), std::max(gx.range.hi, gy.range.hi)}});
        return out.size() == 1 && x.size() == 1 && y.size() == 1 ? out : std::vector<Guard>{};
    }
    enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
    Cmp op;
    switch (b.op) {
        case BinaryOperator::LessThan: op = truth ? Cmp::Lt : Cmp::Ge; break;
        case BinaryOperator::LessThanEqual: op = truth ? Cmp::Le : Cmp::Gt; break;
        case BinaryOperator::GreaterThan: op = truth ? Cmp::Gt : Cmp::Le; break;
        case BinaryOperator::GreaterThanEqual: op = truth ? Cmp::Ge : Cmp::Lt; break;
        case BinaryOperator::Equality: op = truth ? Cmp::Eq : Cmp::Ne; break;
        case BinaryOperator::Inequality: op = truth ? Cmp::Ne : Cmp::Eq; break;
        default: return {};
    }
    // A variable against a constant, each side reaching the comparison unchanged, so the comparison is the arithmetic one.
    const Expression* var = &unwrapImplicit(b.left());
    const Expression* other = &b.right();
    bool swapped = false;
    if (var->kind != ExpressionKind::NamedValue) {
        var = &unwrapImplicit(b.right());
        other = &b.left();
        swapped = true;
    }
    if (var->kind != ExpressionKind::NamedValue || !var->type || !var->type->isIntegral()) return {};
    const auto& sym = var->as<NamedValueExpression>().symbol;
    if (loops_.count(&sym) || (sym.kind != SymbolKind::Variable && sym.kind != SymbolKind::Net && sym.kind != SymbolKind::FormalArgument)) return {};
    const auto value = constantWide(l_, unwrapImplicit(*other));
    if (!value || !preservedThrough(swapped ? b.right() : b.left()) || !preservedThrough(*other)) return {};
    if (swapped) {
        if (op == Cmp::Lt) op = Cmp::Gt;
        else if (op == Cmp::Gt) op = Cmp::Lt;
        else if (op == Cmp::Le) op = Cmp::Ge;
        else if (op == Cmp::Ge) op = Cmp::Le;
    }
    const auto full = typeRange(*var->type);
    if (!full) return {};
    Interval range = *full;
    switch (op) {
        case Cmp::Lt: range.hi = *value - 1; break;
        case Cmp::Le: range.hi = *value; break;
        case Cmp::Gt: range.lo = *value + 1; break;
        case Cmp::Ge: range.lo = *value; break;
        case Cmp::Eq: range = {*value, *value}; break;
        // an interval has no hole: excluding the value narrows it only at an end of the type's range (`d != 0` for an unsigned `d`)
        case Cmp::Ne:
            if (*value == full->lo) range.lo = *value + 1;
            else if (*value == full->hi) range.hi = *value - 1;
            else return {};
            break;
    }
    if (range.lo > range.hi) return {};   // no value satisfies it: the branch never runs, nothing to narrow
    return {{static_cast<const ValueSymbol*>(&sym), range}};
}

bool ValueRanges::wraps(const Expression& e) const {
    bool hit = false;
    e.visitSymbolReferences([&](const Expression&, const Symbol& sym) { hit = hit || wrapping_.count(static_cast<const ValueSymbol*>(&sym)) != 0; });
    return hit;
}

bool ValueRanges::determined(const Expression& e) const {
    if (!e.type || !e.type->isIntegral()) return false;
    if (constantWide(l_, e)) return true;
    switch (e.kind) {
        case ExpressionKind::NamedValue: return loops_.count(&e.as<NamedValueExpression>().symbol) != 0;
        case ExpressionKind::Conversion: return determined(e.as<ConversionExpression>().operand());
        case ExpressionKind::UnaryOp: return determined(e.as<UnaryExpression>().operand());
        case ExpressionKind::BinaryOp: return determined(e.as<BinaryExpression>().left()) && determined(e.as<BinaryExpression>().right());
        case ExpressionKind::ConditionalOp: {
            const auto& c = e.as<ConditionalExpression>();
            for (const auto& cond : c.conditions)
                if (cond.pattern || !determined(*cond.expr)) return false;
            return determined(c.left()) && determined(c.right());
        }
        case ExpressionKind::Concatenation:
            for (const auto* op : e.as<ConcatenationExpression>().operands())
                if (!determined(*op)) return false;
            return true;
        default: return false;
    }
}

// The leaf elements an element select of a constant unpacked table can pick: its index's range decides which positions. Tables with an
// index range that does not start at zero, packed arrays and anything not constant are not read.
bool ValueRanges::tableElements(const Expression& e, std::vector<const ConstantValue*>& out) const {
    if (const ConstantValue* whole = e.getConstant()) {
        out.push_back(whole);
        return true;
    }
    if (e.kind != ExpressionKind::ElementSelect) return false;
    const auto& select = e.as<ElementSelectExpression>();
    const auto& valueType = select.value().type->getCanonicalType();
    if (valueType.kind != SymbolKind::FixedSizeUnpackedArrayType) return false;
    const auto range = valueType.getFixedRange();
    if (range.lower() != 0 || range.width() == 0) return false;
    std::vector<const ConstantValue*> inner;
    if (!tableElements(select.value(), inner)) return false;
    const auto index = of(select.selector());
    if (!index) return false;
    const Wide lo = std::max<Wide>(index->lo, 0), hi = std::min<Wide>(index->hi, Wide(range.width()) - 1);
    if (lo > hi || hi - lo > 4096) return false;
    for (const auto* table : inner) {
        if (!table->isUnpacked()) return false;
        const auto elements = table->elements();
        if (elements.size() != range.width()) return false;
        for (Wide k = lo; k <= hi; ++k) out.push_back(&elements[static_cast<size_t>(k)]);
    }
    return true;
}

std::optional<Interval> ValueRanges::assignedOnceRange(const VariableSymbol& var) const {
    static thread_local int depth = 0;
    if (depth > 4 || var.getInitializer()) return std::nullopt;   // static or automatic: every read follows the write (checked below)
    const auto* scope = var.getParentScope();
    const auto* block = scope ? scope->asSymbol().as_if<StatementBlockSymbol>() : nullptr;
    const Statement* body = block ? block->tryGetStatement() : nullptr;
    if (!body) return std::nullopt;
    struct Mentions : ASTVisitor<Mentions, VisitFlags::AllGood> {
        const Symbol* var; bool found = false;
        explicit Mentions(const Symbol* v) : var(v) {}
        void handle(const NamedValueExpression& e) { found = found || &e.symbol == var; }
        void handle(const HierarchicalValueExpression& e) { found = found || e.getSymbolReference() == var; }
    };
    // Any write to the local other than one plain blocking `var = expr;` makes the single-write range void: a write through a
    // concatenation, a select, a streaming or assignment-pattern target, an increment, an output or ref argument of a call.
    struct Writes : ASTVisitor<Writes, VisitFlags::AllGood> {
        const Symbol* var;
        std::vector<const AssignmentExpression*> found;
        bool other = false;
        bool mentions(const Expression& e) const {
            Mentions m(var);
            e.visit(m);
            return m.found;
        }
        // True when the target names `var` in a place that is written.
        bool writesVar(const Expression& target) const {
            switch (target.kind) {
                case ExpressionKind::NamedValue: case ExpressionKind::HierarchicalValue: case ExpressionKind::Concatenation:
                case ExpressionKind::MemberAccess: case ExpressionKind::ElementSelect: case ExpressionKind::RangeSelect:
                case ExpressionKind::Conversion: {
                    const auto syms = lvalueSymbols(target);
                    return std::find(syms.begin(), syms.end(), var) != syms.end();
                }
                default: return mentions(target);   // streaming, assignment patterns and anything else: any mention counts
            }
        }
        void handle(const AssignmentExpression& a) {
            if (a.left().kind == ExpressionKind::NamedValue && a.left().getSymbolReference() == var) {
                if (a.isCompound() || a.isNonBlocking()) other = true;
                else found.push_back(&a);
            }
            else if (writesVar(a.left())) other = true;
            visitDefault(a);
        }
        void handle(const UnaryExpression& u) {
            if ((u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Predecrement ||
                 u.op == UnaryOperator::Postdecrement) && writesVar(u.operand())) other = true;
            visitDefault(u);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall())
                if (const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine); sub && *sub) {
                    const auto formals = (*sub)->getArguments();
                    const auto args = c.arguments();
                    for (size_t i = 0; i < formals.size() && i < args.size(); ++i)
                        if (formals[i]->direction != ArgumentDirection::In && mentions(*args[i])) other = true;
                }
            visitDefault(c);
        }
    } writes;
    writes.var = &var;
    body->visit(writes);
    if (writes.other || writes.found.size() != 1 || lint::blockingWritesIn(*body).unknown) return std::nullopt;
    if (!sverity::analysis::readsOf(*body).dominated(&var)) return std::nullopt;
    const Expression& rhs = writes.found[0]->right();
    struct RhsMentions : ASTVisitor<RhsMentions, VisitFlags::AllGood> {
        const Symbol* var; bool found = false;
        void handle(const NamedValueExpression& e) { found = found || &e.symbol == var; }
        void handle(const CallExpression& c) { if (!c.isSystemCall()) found = true; visitDefault(c); }
    } mentions;
    mentions.var = &var;
    rhs.visit(mentions);
    if (mentions.found) return std::nullopt;
    ++depth;
    const auto range = of(rhs);
    --depth;
    return range;
}

// `i == K`, `i != K` or an ordering of a `for` variable with a constant, when the condition evaluates to `truth`: the variable and the values
// it can take then. `!=` cuts a value off only at an end of the variable's range.
std::optional<std::pair<const ValueSymbol*, Interval>> ValueRanges::loopComparison(const Expression& cond, bool truth) const {
    const Expression& e = unwrapImplicit(cond);
    if (e.kind != ExpressionKind::BinaryOp) return std::nullopt;
    const auto& b = e.as<BinaryExpression>();
    enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
    Cmp op;
    switch (b.op) {
        case BinaryOperator::LessThan: op = truth ? Cmp::Lt : Cmp::Ge; break;
        case BinaryOperator::LessThanEqual: op = truth ? Cmp::Le : Cmp::Gt; break;
        case BinaryOperator::GreaterThan: op = truth ? Cmp::Gt : Cmp::Le; break;
        case BinaryOperator::GreaterThanEqual: op = truth ? Cmp::Ge : Cmp::Lt; break;
        case BinaryOperator::Equality: op = truth ? Cmp::Eq : Cmp::Ne; break;
        case BinaryOperator::Inequality: op = truth ? Cmp::Ne : Cmp::Eq; break;
        default: return std::nullopt;
    }
    const Expression* var = &unwrapImplicit(b.left());
    const Expression* other = &b.right();
    bool swapped = false;
    if (var->kind != ExpressionKind::NamedValue) { var = &unwrapImplicit(b.right()); other = &b.left(); swapped = true; }
    if (var->kind != ExpressionKind::NamedValue) return std::nullopt;
    const auto* sym = static_cast<const ValueSymbol*>(&var->as<NamedValueExpression>().symbol);
    const auto it = loops_.find(sym);
    if (it == loops_.end()) return std::nullopt;
    const auto value = constantWide(l_, unwrapImplicit(*other));
    if (!value || !preservedThrough(swapped ? b.right() : b.left()) || !preservedThrough(*other)) return std::nullopt;
    if (swapped) {
        if (op == Cmp::Lt) op = Cmp::Gt; else if (op == Cmp::Gt) op = Cmp::Lt;
        else if (op == Cmp::Le) op = Cmp::Ge; else if (op == Cmp::Ge) op = Cmp::Le;
    }
    const Interval now = it->second.inTest ? it->second.test : it->second.body;
    Interval allowed = now;
    switch (op) {
        case Cmp::Lt: allowed.hi = *value - 1; break;
        case Cmp::Le: allowed.hi = *value; break;
        case Cmp::Gt: allowed.lo = *value + 1; break;
        case Cmp::Ge: allowed.lo = *value; break;
        case Cmp::Eq: allowed = {*value, *value}; break;
        case Cmp::Ne:
            if (*value == now.lo) allowed.lo = *value + 1;
            else if (*value == now.hi) allowed.hi = *value - 1;
            else return std::nullopt;
            break;
    }
    return std::pair<const ValueSymbol*, Interval>{sym, allowed};
}

// ---------------------------------------------------------------------------------------------------------------------------
// Accumulator bound. A register R that one clocked procedure only ever sets to constants (together with a constant counter C) or
// advances by a bounded amount while C advances by one, and clears when C reaches its limit, never exceeds Rc + E * (U - Cmin).
// The proof reads the structure, not intent; any write, guard or width it cannot place voids it (docs/rules/ANALYSIS.md).
namespace {

struct AccumulatorCache {
    std::unordered_map<const VariableSymbol*, std::optional<Interval>> byVar;
};

class AccumulatorProof {
public:
    AccumulatorProof(Lint& l, const VariableSymbol& reg) : l_(l), reg_(reg), plain_(l, false, false) {}

    std::optional<Interval> run() {
        if (!plainRegister(reg_)) return std::nullopt;
        const auto* proc = singleProcedure(reg_);
        if (!proc) return std::nullopt;
        if (!shapeOk(*proc)) return std::nullopt;
        // Candidate counters: a variable advanced by one beside R's accumulation.
        std::vector<const VariableSymbol*> candidates;
        gatherCandidates(proc->getBody(), candidates);
        for (const auto* counter : candidates) {
            if (counter == &reg_ || !plainRegister(*counter) || singleProcedure(*counter) != proc) continue;
            if (const auto bound = prove(*proc, *counter)) return bound;
        }
        return std::nullopt;
    }

private:
    struct Guard { const Expression* cond; bool truth; };
    struct Group {
        bool accumulate = false;
        Wide rk = 0, ck = 0, e = 0;   // constants written, or the largest addend
        std::vector<Guard> path;
    };

    Lint& l_;
    const VariableSymbol& reg_;
    ValueRanges plain_;
    const VariableSymbol* counter_ = nullptr;
    std::vector<Group> groups_;
    size_t accepted_ = 0;
    bool failed_ = false;

    static bool plainRegister(const VariableSymbol& v) {
        const Type& t = v.getType().getCanonicalType();
        return !v.getInitializer() && t.isIntegral() && !t.isSigned() && t.getBitWidth() > 0 && t.getBitWidth() <= 62 &&
               v.lifetime == VariableLifetime::Static && v.getParentScope() &&
               v.getParentScope()->asSymbol().kind != SymbolKind::StatementBlock;
    }
    static Wide maxOf(const VariableSymbol& v) { return (Wide(1) << v.getType().getCanonicalType().getBitWidth()) - 1; }

    // Every driver of `v` is one write by the same always_ff / always process.
    const ProceduralBlockSymbol* singleProcedure(const VariableSymbol& v) const {
        const auto* an = l_.context().design.analysis.get();
        if (!an) return nullptr;
        const auto drivers = an->getDrivers(v);
        if (drivers.empty()) return nullptr;
        const ProceduralBlockSymbol* proc = nullptr;
        for (const auto* d : drivers) {
            if (d->kind != slang::analysis::DriverKind::Procedural || d->isInputPort()) return nullptr;
            const auto* p = d->containingSymbol->as_if<ProceduralBlockSymbol>();
            if (!p || (p->procedureKind != ProceduralBlockKind::AlwaysFF && p->procedureKind != ProceduralBlockKind::Always)) return nullptr;
            if (proc && proc != p) return nullptr;
            proc = p;
        }
        return proc;
    }

    // No user call, no control transfer out of the middle of a group, no delay, no force on R in its instance.
    bool shapeOk(const ProceduralBlockSymbol& proc) const {
        struct Shape : ASTVisitor<Shape, VisitFlags::AllGood> {
            bool bad = false;
            void handle(const CallExpression& c) { if (!c.isSystemCall()) bad = true; visitDefault(c); }
            void handle(const DisableStatement&) { bad = true; }
            void handle(const ReturnStatement&) { bad = true; }
            void handle(const BreakStatement&) { bad = true; }
            void handle(const ContinueStatement&) { bad = true; }
            void handle(const WaitStatement&) { bad = true; }
            void handle(const ProceduralAssignStatement&) { bad = true; }
            void handle(const ProceduralDeassignStatement&) { bad = true; }
            void handle(const TimedStatement& t) {
                // only the process's own event control, at the top
                if (depth++ > 0) bad = true;
                visitDefault(t);
            }
            int depth = 0;
        } shape;
        proc.getBody().visit(shape);
        return !shape.bad;
    }

    bool forcedElsewhere(const VariableSymbol& v) const {
        const Scope* scope = v.getParentScope();
        while (scope && scope->asSymbol().kind != SymbolKind::InstanceBody) scope = scope->asSymbol().getParentScope();
        if (!scope) return true;
        struct Force : ASTVisitor<Force, VisitFlags::AllGood> {
            const Symbol* var; bool hit = false;
            void mention(const Statement& s, const Expression& target) {
                (void)s;
                for (const auto* sym : lvalueSymbols(target)) hit = hit || sym == var;
            }
            void handle(const ProceduralAssignStatement& s) { mention(s, s.assignment); visitDefault(s); }
            void handle(const ProceduralDeassignStatement& s) { mention(s, s.lvalue); visitDefault(s); }
        } force;
        force.var = &v;
        scope->asSymbol().visit(force);
        return force.hit;
    }

    static const Expression& core(const Expression& e) { return unwrapImplicit(e); }
    static const NamedValueExpression* named(const Expression& e, const Symbol* sym) {
        const Expression& x = core(e);
        return x.kind == ExpressionKind::NamedValue && &x.as<NamedValueExpression>().symbol == sym ? &x.as<NamedValueExpression>() : nullptr;
    }
    // `V + other` or `other + V` (the sum as written, no explicit narrowing around V); `other` is returned.
    static const Expression* addendOf(const Expression& rhs, const Symbol* v) {
        const Expression& x = core(rhs);
        if (x.kind != ExpressionKind::BinaryOp || x.as<BinaryExpression>().op != BinaryOperator::Add || x.type->isSigned()) return nullptr;
        const auto& b = x.as<BinaryExpression>();
        if (named(b.left(), v)) return &b.right();
        if (named(b.right(), v)) return &b.left();
        return nullptr;
    }

    void gatherCandidates(const Statement& s, std::vector<const VariableSymbol*>& out) const {
        struct Cand : ASTVisitor<Cand, VisitFlags::AllGood> {
            const AccumulatorProof& p;
            std::vector<const VariableSymbol*>& out;
            Cand(const AccumulatorProof& pr, std::vector<const VariableSymbol*>& o) : p(pr), out(o) {}
            void handle(const AssignmentExpression& a) {
                if (a.isNonBlocking() && a.left().kind == ExpressionKind::NamedValue) {
                    const auto* sym = &a.left().as<NamedValueExpression>().symbol;
                    if (sym->kind == SymbolKind::Variable && sym != &p.reg_)
                        if (const auto* other = addendOf(a.right(), sym); other && constantWide(p.l_, core(*other)) == Wide(1))
                            if (std::find(out.begin(), out.end(), &sym->as<VariableSymbol>()) == out.end()) out.push_back(&sym->as<VariableSymbol>());
                }
                visitDefault(a);
            }
        } cand(*this, out);
        s.visit(cand);
    }

    // The writes of R and C that the statement contains, whatever their form (assignment, compound, increment, output argument).
    size_t countWrites(const Statement& s) const {
        struct Count : ASTVisitor<Count, VisitFlags::AllGood> {
            const Symbol* r; const Symbol* c; size_t n = 0;
            void note(const Expression& target) {
                for (const auto* sym : lvalueSymbols(target)) if (sym == r || sym == c) ++n;
            }
            void handle(const AssignmentExpression& a) { note(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
                    u.op == UnaryOperator::Postdecrement) note(u.operand());
                visitDefault(u);
            }
            void handle(const CallExpression& call) {
                // an argument of a system call may be written (`$fscanf`): any mention counts as a write
                if (call.isSystemCall())
                    for (const auto* arg : call.arguments())
                        if (arg) note(*arg);
                visitDefault(call);
            }
        } count;
        count.r = &reg_;
        count.c = counter_;
        s.visit(count);
        return count.n;
    }

    // One group: the statements of a list (or one branch). R and C written in it are written together.
    void scanGroup(std::span<const Statement* const> stmts, std::vector<Guard>& path, int depth) {
        if (failed_ || depth > 12) { failed_ = true; return; }
        std::vector<const AssignmentExpression*> rWrites, cWrites;
        for (const Statement* s : stmts) {
            const Statement* x = s;
            while (x->kind == StatementKind::Block || (x->kind == StatementKind::List && x->as<StatementList>().list.size() == 1)) {
                if (x->kind == StatementKind::Block) {
                    x = &x->as<BlockStatement>().body;
                    // a nested block is a group of its own
                    if (x->kind == StatementKind::List) {
                        scanGroup(x->as<StatementList>().list, path, depth + 1);
                        x = nullptr;
                        break;
                    }
                }
                else x = x->as<StatementList>().list[0];
            }
            if (!x) continue;
            switch (x->kind) {
                case StatementKind::List: scanGroup(x->as<StatementList>().list, path, depth + 1); break;
                case StatementKind::Empty: case StatementKind::VariableDeclaration: break;
                case StatementKind::ExpressionStatement: {
                    const auto& e = x->as<ExpressionStatement>().expr;
                    if (e.kind == ExpressionKind::Assignment) {
                        const auto& a = e.as<AssignmentExpression>();
                        if (a.left().kind == ExpressionKind::NamedValue && &a.left().as<NamedValueExpression>().symbol == &reg_) {
                            if (!a.isNonBlocking() || a.isCompound() || a.timingControl) { failed_ = true; return; }
                            rWrites.push_back(&a);
                            break;
                        }
                        if (a.left().kind == ExpressionKind::NamedValue && &a.left().as<NamedValueExpression>().symbol == counter_) {
                            if (!a.isNonBlocking() || a.isCompound() || a.timingControl) { failed_ = true; return; }
                            cWrites.push_back(&a);
                            break;
                        }
                    }
                    if (countWrites(*x) != 0) failed_ = true;
                    break;
                }
                case StatementKind::Conditional: {
                    const auto& c = x->as<ConditionalStatement>();
                    const bool simple = c.conditions.size() == 1 && !c.conditions[0].pattern;
                    if (simple) path.push_back({c.conditions[0].expr, true});
                    scanBranch(c.ifTrue, path, depth);
                    if (simple) path.back().truth = false;
                    if (c.ifFalse) scanBranch(*c.ifFalse, path, depth);
                    if (simple) path.pop_back();
                    break;
                }
                default:
                    if (countWrites(*x) != 0) failed_ = true;   // a case, loop or anything else holding a write of R or C
                    break;
            }
            if (failed_) return;
        }
        if (rWrites.empty() && cWrites.empty()) return;
        if (rWrites.size() != 1 || cWrites.size() != 1) { failed_ = true; return; }
        accepted_ += 2;
        Group g;
        g.path = path;
        const Expression& rr = rWrites[0]->right();
        const Expression& cr = cWrites[0]->right();
        const auto rc = constantWide(l_, rr), cc = constantWide(l_, cr);
        if (rc && cc) {
            if (*rc < 0 || *rc > maxOf(reg_) || *cc < 0 || *cc > maxOf(*counter_)) { failed_ = true; return; }
            g.rk = *rc;
            g.ck = *cc;
        }
        else {
            const auto* add = addendOf(rr, &reg_);
            const auto* one = addendOf(cr, counter_);
            if (!add || !one || constantWide(l_, core(*one)) != Wide(1) || !core(*add).type || core(*add).type->isSigned()) { failed_ = true; return; }
            const auto range = plain_.of(core(*add));
            if (!range || range->lo < 0) { failed_ = true; return; }
            g.accumulate = true;
            g.e = range->hi;
        }
        groups_.push_back(std::move(g));
    }
    void scanBranch(const Statement& s, std::vector<Guard>& path, int depth) {
        if (s.kind == StatementKind::List) scanGroup(s.as<StatementList>().list, path, depth + 1);
        else {
            const Statement* one = &s;
            scanGroup(std::span<const Statement* const>(&one, 1), path, depth + 1);
        }
    }

    struct Limit { std::optional<Wide> hi; std::optional<Wide> ne; };
    // What the guards say of C: an upper bound (`C < K`, `C <= K`, `C == K`) or `C != K`.
    void limits(const Expression& cond, bool truth, Limit& out, int depth) const {
        if (depth > 6) return;
        const Expression& e = core(cond);
        if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
            limits(e.as<UnaryExpression>().operand(), !truth, out, depth + 1);
            return;
        }
        if (e.kind != ExpressionKind::BinaryOp) return;
        const auto& b = e.as<BinaryExpression>();
        if (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) {
            if ((b.op == BinaryOperator::LogicalAnd) == truth) { limits(b.left(), truth, out, depth + 1); limits(b.right(), truth, out, depth + 1); }
            return;
        }
        enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
        Cmp op;
        switch (b.op) {
            case BinaryOperator::LessThan: op = truth ? Cmp::Lt : Cmp::Ge; break;
            case BinaryOperator::LessThanEqual: op = truth ? Cmp::Le : Cmp::Gt; break;
            case BinaryOperator::GreaterThan: op = truth ? Cmp::Gt : Cmp::Le; break;
            case BinaryOperator::GreaterThanEqual: op = truth ? Cmp::Ge : Cmp::Lt; break;
            case BinaryOperator::Equality: op = truth ? Cmp::Eq : Cmp::Ne; break;
            case BinaryOperator::Inequality: op = truth ? Cmp::Ne : Cmp::Eq; break;
            default: return;
        }
        const Expression* other = &b.right();
        bool swapped = false;
        if (!named(b.left(), counter_)) {
            if (!named(b.right(), counter_)) return;
            other = &b.left();
            swapped = true;
        }
        const auto k = constantWide(l_, core(*other));
        if (!k || *k < 0) return;
        if (swapped) {
            if (op == Cmp::Lt) op = Cmp::Gt; else if (op == Cmp::Gt) op = Cmp::Lt;
            else if (op == Cmp::Le) op = Cmp::Ge; else if (op == Cmp::Ge) op = Cmp::Le;
        }
        const auto tighten = [&](Wide h) { out.hi = out.hi ? std::min(*out.hi, h) : h; };
        switch (op) {
            case Cmp::Lt: if (*k > 0) tighten(*k - 1); break;
            case Cmp::Le: case Cmp::Eq: tighten(*k); break;
            case Cmp::Ne: if (!out.ne || *out.ne > *k) out.ne = *k; break;
            default: break;
        }
    }

    std::optional<Interval> prove(const ProceduralBlockSymbol& proc, const VariableSymbol& counter) {
        counter_ = &counter;
        groups_.clear();
        accepted_ = 0;
        failed_ = false;
        if (forcedElsewhere(reg_) || forcedElsewhere(counter)) return std::nullopt;
        // root: the process's event control, then one conditional on an asynchronous reset
        const Statement* body = &proc.getBody();
        if (body->kind != StatementKind::Timed) return std::nullopt;
        const auto& timed = body->as<TimedStatement>();
        std::unordered_set<const Symbol*> resets;
        if (timed.timing.kind != TimingControlKind::EventList) return std::nullopt;
        const auto& events = timed.timing.as<EventListControl>().events;
        if (events.size() < 2) return std::nullopt;
        for (size_t i = 1; i < events.size(); ++i) {
            if (events[i]->kind != TimingControlKind::SignalEvent) return std::nullopt;
            events[i]->as<SignalEventControl>().expr.visitSymbolReferences([&](const Expression&, const Symbol& s) { resets.insert(&s); });
        }
        const Statement* root = &timed.stmt;
        for (;;) {
            if (root->kind == StatementKind::Block) root = &root->as<BlockStatement>().body;
            else if (root->kind == StatementKind::List && root->as<StatementList>().list.size() == 1) root = root->as<StatementList>().list[0];
            else break;
        }
        if (root->kind != StatementKind::Conditional) return std::nullopt;
        const auto& rc = root->as<ConditionalStatement>();
        if (rc.conditions.size() != 1 || rc.conditions[0].pattern) return std::nullopt;
        bool onReset = false;
        rc.conditions[0].expr->visitSymbolReferences([&](const Expression&, const Symbol& s) { onReset = onReset || resets.count(&s) != 0; });
        if (!onReset) return std::nullopt;
        std::vector<Guard> path;
        scanGroup(std::span<const Statement* const>(&root, 1), path, 0);
        if (failed_ || countWrites(proc.getBody()) != accepted_) return std::nullopt;
        // a constant pair directly under the reset conditional gives the state after reset
        bool reset = false;
        Wide rcMax = 0, cMin = maxOf(counter), cMaxConst = 0, e = 0;
        for (const auto& g : groups_) {
            if (g.accumulate) { e = std::max(e, g.e); continue; }
            if (g.path.size() == 1 && g.path[0].cond == rc.conditions[0].expr.get()) reset = true;
            rcMax = std::max(rcMax, g.rk);
            cMin = std::min(cMin, g.ck);
            cMaxConst = std::max(cMaxConst, g.ck);
        }
        if (!reset) return std::nullopt;
        // the counter's upper bound
        Wide upper = cMaxConst;
        std::optional<Wide> neLimit;
        bool any = false;
        for (const auto& g : groups_) {
            if (!g.accumulate) continue;
            any = true;
            Limit lim;
            for (const auto& step : g.path) limits(*step.cond, step.truth, lim, 0);
            if (lim.hi) {
                if (*lim.hi + 1 > maxOf(counter)) return std::nullopt;
                upper = std::max(upper, *lim.hi + 1);
            }
            else if (lim.ne) {
                if (neLimit && *neLimit != *lim.ne) return std::nullopt;
                neLimit = lim.ne;
            }
            else return std::nullopt;
        }
        if (!any) return std::nullopt;
        if (neLimit) {
            // `C != K` bounds C below K only when C never exceeds K: every constant and every other limit stays at or below it
            if (*neLimit > maxOf(counter) || upper > *neLimit) return std::nullopt;
            upper = *neLimit;
        }
        const Wide bound = rcMax + e * (upper - cMin);
        if (bound > maxOf(reg_)) return std::nullopt;
        return Interval{0, bound};
    }
};

AccumulatorCache& accumulatorCache(Lint& l) {
    auto& slot = model::getModel(l.context().design).accumulatorRanges;
    if (!slot) slot = std::make_shared<AccumulatorCache>();
    return *static_cast<AccumulatorCache*>(slot.get());
}

std::optional<Interval> accumulatorBound(Lint& l, const VariableSymbol& v) {
    static std::recursive_mutex guard;
    static thread_local std::unordered_set<const VariableSymbol*> inProgress;
    std::lock_guard<std::recursive_mutex> lock(guard);
    auto& cache = accumulatorCache(l);
    if (auto it = cache.byVar.find(&v); it != cache.byVar.end()) return it->second;
    if (!inProgress.insert(&v).second) return std::nullopt;   // a cycle: the conservative answer, not cached
    const auto bound = AccumulatorProof(l, v).run();
    inProgress.erase(&v);
    cache.byVar[&v] = bound;
    return bound;
}

} // namespace

std::optional<Interval> ValueRanges::of(const Expression& e) const {
    if (!e.type || !e.type->isIntegral()) return std::nullopt;
    const auto full = typeRange(*e.type);
    // A constant is exact whatever the width of its type.
    if (!full) {
        if (const auto c = constantWide(l_, e)) return Interval{*c, *c};
        return std::nullopt;
    }

    if (e.kind == ExpressionKind::NamedValue) {
        if (auto it = loops_.find(&e.as<NamedValueExpression>().symbol); it != loops_.end()) {
            Interval r = it->second.inTest ? it->second.test : it->second.body;
            if (auto g = guards_.find(&e.as<NamedValueExpression>().symbol); g != guards_.end()) {
                const Interval cut{std::max(r.lo, g->second.lo), std::min(r.hi, g->second.hi)};
                if (cut.lo <= cut.hi) r = cut;
            }
            return r;
        }
        const auto& symbol = e.as<NamedValueExpression>().symbol;
        Interval current = *full;
        if (formals_ && symbol.kind == SymbolKind::FormalArgument) {
            const auto& known = formalRangesOf(l_).byFormal;
            if (auto it = known.find(static_cast<const ValueSymbol*>(&symbol)); it != known.end() && it->second.within(*full)) current = it->second;
        }
        // A net or variable whose only driver is a continuous assignment of a constant (`assign woffset = '0;` in the branch of a generate
        // that is elaborated) holds that constant.
        if (const auto* only = soleContinuousValue(l_, static_cast<const ValueSymbol&>(symbol))) {
            if (const auto c = constantWide(l_, *only))
                if (Interval{*c, *c}.within(*full)) return Interval{*c, *c};
            // Any other value follows the net: `assign xe = {4'b0, x};` keeps xe in [0, 15] (a chain of nets is followed a few steps).
            static thread_local int netDepth = 0;
            if (netDepth < 4) {
                ++netDepth;
                const auto through = of(*only);
                --netDepth;
                if (through && through->within(*full)) current = *through;
            }
        }
        // An automatic variable of a block that one blocking assignment writes before every read (`int index_z; index_z = (z == 0) ? W-1 : z-1;`)
        // holds what that assignment gives, in the scope the read sits in.
        if (symbol.kind == SymbolKind::Variable)
            if (const auto v = assignedOnceRange(static_cast<const VariableSymbol&>(symbol)); v && v->within(*full)) return *v;
        // A register one clocked process only clears or accumulates in bounded steps beside a bounded counter (see AccumulatorProof).
        if (symbol.kind == SymbolKind::Variable)
            if (const auto v = accumulatorBound(l_, static_cast<const VariableSymbol&>(symbol)); v && v->within(*full)) current = *v;
        if (auto it = guards_.find(static_cast<const ValueSymbol*>(&symbol)); it != guards_.end()) {
            const Interval cut{std::max(current.lo, it->second.lo), std::min(current.hi, it->second.hi)};
            if (cut.lo <= cut.hi) current = cut;   // an empty cut is a branch that never runs: keep the wider, sound range
        }
        if (!(current == *full)) return current;
    }
    if (auto c = constantWide(l_, e)) return Interval{*c, *c}.within(*full) ? Interval{*c, *c} : *full;

    switch (e.kind) {
        case ExpressionKind::Conversion: {
            const auto& c = e.as<ConversionExpression>();
            if (const auto* base = signExtendedBase(l_, *c.type, c.operand())) return of(*base);
            const auto in = of(c.operand());
            if (!in) return full;
            return in->within(*full) ? *in : *full;
        }
        case ExpressionKind::Call: {
            const auto& call = e.as<CallExpression>();
            if (call.isSystemCall() && call.getSubroutineName() == "$signed" && call.arguments().size() == 1) {
                if (const auto* base = signExtendedBase(l_, *e.type, *call.arguments()[0])) return of(*base);
            }
            // $clog2 never decreases with its argument, so the range of the argument bounds it.
            if (call.isSystemCall() && call.getSubroutineName() == "$clog2" && call.arguments().size() == 1) {
                const auto in = of(*call.arguments()[0]);
                if (!in || in->lo < 0) return full;
                const auto clog2 = [](Wide v) {
                    Wide bits = 0;
                    for (Wide x = 1; x < v; x <<= 1) ++bits;
                    return bits;
                };
                const Interval m{clog2(in->lo), clog2(in->hi)};
                return m.within(*full) ? m : *full;
            }
            return full;
        }
        case ExpressionKind::BinaryOp: return binary(e.as<BinaryExpression>(), true);
        case ExpressionKind::ElementSelect: {
            // an element of a constant table (a localparam array) chosen by an index of known range: the values of the elements it can pick
            std::vector<const ConstantValue*> elements;
            if (!tableElements(e, elements) || elements.empty()) return full;
            Interval m{0, 0};
            bool first = true;
            for (const auto* v : elements) {
                if (!v->isInteger() || v->integer().hasUnknown()) return full;
                const auto n = v->integer().isSigned() ? Wide(v->integer().as<int64_t>().value_or(0)) : Wide(v->integer().as<uint64_t>().value_or(0));
                if (first) m = {n, n};
                else m = {std::min(m.lo, n), std::max(m.hi, n)};
                first = false;
            }
            return m.within(*full) ? m : *full;
        }
        case ExpressionKind::UnaryOp: {
            const auto& u = e.as<UnaryExpression>();
            const auto v = valueIn(u.operand(), *e.type);
            if (!v) return full;
            Interval m = *full;
            if (u.op == UnaryOperator::Plus) m = *v;
            else if (u.op == UnaryOperator::Minus) m = {-v->hi, -v->lo};
            else if (u.op == UnaryOperator::BitwiseNot) m = e.type->isSigned() ? Interval{-v->hi - 1, -v->lo - 1} : Interval{full->hi - v->hi, full->hi - v->lo};
            else return full;
            return m.within(*full) ? m : *full;
        }
        case ExpressionKind::ConditionalOp: {
            const auto& c = e.as<ConditionalExpression>();
            // each arm is read under what its condition says of the variables it names
            std::optional<Interval> a, b;
            if (c.conditions.size() == 1 && !c.conditions[0].pattern) {
                auto& self = const_cast<ValueRanges&>(*this);
                const auto arm = [&](const Expression& value, bool truth) {
                    Narrowing n(self, guardsOf(*c.conditions[0].expr, truth));
                    // a `for` variable compared with a constant: inside the arm it takes only the values the comparison allows
                    const ValueSymbol* var = nullptr;
                    std::optional<Interval> saved;
                    if (const auto allowed = loopComparison(*c.conditions[0].expr, truth)) {
                        bool empty = false;
                        saved = self.narrow(allowed->first, allowed->second, empty);
                        if (saved) var = allowed->first;
                    }
                    const auto r = valueIn(value, *e.type);
                    if (var) self.restore(var, saved);
                    return r;
                };
                a = arm(c.left(), true);
                b = arm(c.right(), false);
            }
            else { a = valueIn(c.left(), *e.type); b = valueIn(c.right(), *e.type); }
            if (!a || !b) return full;
            return Interval{std::min(a->lo, b->lo), std::max(a->hi, b->hi)};
        }
        case ExpressionKind::Concatenation: {
            // Each operand contributes its unsigned bit pattern at its offset.
            Interval sum{0, 0};
            int shift = 0;
            const auto operands = e.as<ConcatenationExpression>().operands();
            for (auto it = operands.rbegin(); it != operands.rend(); ++it) {
                const Expression& op = **it;
                if (!op.type || !op.type->isIntegral()) return full;
                const auto w = op.type->getBitWidth();
                if (w == 0) continue;
                if (shift + w > 64) return full;
                Interval pattern{0, (Wide(1) << w) - 1};
                if (const auto v = of(op); v && v->lo >= 0 && v->hi <= pattern.hi) pattern = *v;
                sum.lo += pattern.lo << shift;
                sum.hi += pattern.hi << shift;
                shift += static_cast<int>(w);
            }
            return sum.within(*full) ? sum : *full;
        }
        default: return full;
    }
}

bool ValueRanges::preservedThrough(const Expression& e) const {
    const Expression* p = &e;
    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit()) {
        const Expression& operand = p->as<ConversionExpression>().operand();
        const auto in = of(operand);
        const auto out = p->type ? typeRange(*p->type) : std::nullopt;
        if (!in || !out || !in->within(*out)) return false;
        p = &operand;
    }
    return true;
}

std::string ValueRanges::text(const Expression& e) const {
    const auto& sm = l_.sm();
    const auto start = e.sourceRange.start(), end = e.sourceRange.end();
    if (!start.valid() || !end.valid() || sm.isMacroLoc(start) || sm.isMacroLoc(end) || start.buffer() != end.buffer() ||
        end.offset() <= start.offset())
        return {};
    auto s = std::string(sm.getSourceText(start.buffer()).substr(start.offset(), end.offset() - start.offset()));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    if (s.size() > 40 || s.find('\n') != std::string::npos) return {};
    return s;
}

} // namespace sverity::lint
