// Native AST lint: width rules. LNT-026 sized-literal fit and narrowing, LNT-034 conditional arms, LNT-035 unsized
// shift operands, LNT-036 arithmetic truncation, LNT-055 unsigned difference below zero, LNT-056 constant concatenations and LNT-057
// parameter-dependent truncation. Slang's implicit width/sign diagnostics (LNT-004/005), shift-count checks (LNT-026),
// signed-logical-shift (LNT-035) and unsized-in-concat (LNT-056) are not repeated here.
#include <algorithm>
#include <span>
#include <cctype>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rules/lint/loop_facts.h"
#include "rules/lint/value_range.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/numeric/SVInt.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/symbols/MemberSymbols.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;

using OptWidth = std::optional<bitwidth_t>;

// The operand as written: implicit (assignment or operand-propagation) conversions removed.
const Expression& unwrap(const Expression& e) { return lint::unwrapImplicit(e); }

OptWidth widthOf(const Expression& e) {
    if (!e.type || !e.type->isIntegral()) return std::nullopt;
    return e.type->getBitWidth();
}

bool isConst(const Lint& l, const Expression& e) { return l.evalConst(e).isInteger(); }


bool isLeftShift(BinaryOperator op) {
    return op == BinaryOperator::LogicalShiftLeft || op == BinaryOperator::ArithmeticShiftLeft;
}

bool isCompare(BinaryOperator op) {
    switch (op) {
        case BinaryOperator::Equality:
        case BinaryOperator::Inequality:
        case BinaryOperator::CaseEquality:
        case BinaryOperator::CaseInequality:
        case BinaryOperator::WildcardEquality:
        case BinaryOperator::WildcardInequality:
        case BinaryOperator::LessThan:
        case BinaryOperator::LessThanEqual:
        case BinaryOperator::GreaterThan:
        case BinaryOperator::GreaterThanEqual: return true;
        default: return false;
    }
}

// Bits needed to hold constant v (sign bit included for signed values).
bitwidth_t bitsNeeded(const SVInt& v) {
    bool neg = v.isSigned() && v.isNegative();
    int hi = -1;
    for (int i = int(v.getBitWidth()) - 1; i >= 0 && hi < 0; --i)
        if (bool(v[int32_t(i)]) != neg) hi = i;
    bitwidth_t n = bitwidth_t(hi + 1) + (v.isSigned() ? 1 : 0);
    return std::max<bitwidth_t>(n, 1);
}

// Width an operand needs in an arithmetic context: constants need only their significant bits.
OptWidth operandBits(const Lint& l, const Expression& e) {
    auto cv = l.evalConst(e);
    if (cv.isInteger() && !cv.integer().hasUnknown()) return bitsNeeded(cv.integer());
    return widthOf(e);
}

// Width the value of `e` needs. Add/subtract/bitwise results need only their widest operand's bits, so an unsized
// literal operand (32 bits by type) does not make `cnt + 1` look wider than `cnt`.
OptWidth neededWidth(const Lint& l, const Expression& e) {
    const Expression& u = unwrap(e);
    if (u.kind == ExpressionKind::BinaryOp) {
        auto& b = u.as<BinaryExpression>();
        if (b.op == BinaryOperator::Add || b.op == BinaryOperator::Subtract || b.op == BinaryOperator::BinaryAnd ||
            b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryXor) {
            auto x = neededWidth(l, b.left()), y = neededWidth(l, b.right());
            if (x && y) return std::max(*x, *y);
        }
    }
    if (isConst(l, u)) return operandBits(l, u);
    return widthOf(u);
}

// Source text of a range (macro locations mapped to the original file); nullopt when it is not contiguous.
std::optional<std::string_view> sourceText(const Lint& l, SourceRange r) {
    const auto& sm = l.sm();
    auto b = sm.getFullyOriginalLoc(r.start());
    auto e = sm.getFullyOriginalLoc(r.end());
    if (!b.valid() || !e.valid() || b.buffer().getId() != e.buffer().getId() || e.offset() < b.offset())
        return std::nullopt;
    return sm.getSourceText(b.buffer()).substr(b.offset(), e.offset() - b.offset());
}

// Reports at `at` with the instance path of the enclosing body, unless the caller already gave a hierarchy.
void reportIn(Lint& l, const std::string& path, slang::SourceLocation at, std::string msg, LintOpts o = {}) {
    if (o.hierarchy.empty()) o.hierarchy = path;
    l.report(at, std::move(msg), std::move(o));
}

// ---------------------------------------------------------------------------------------------------------------
// Sized literal digits: "<size>'<s?><base><digits>" read from the source text. Slang keeps only the truncated value,
// so the fit test works on the digits as written.
struct SizedLit {
    uint32_t size = 0;
    bool isSigned = false, decimal = false;
    std::optional<uint32_t> needed;   // bits the digits need; nullopt when undecidable
};

uint32_t bitLength(uint64_t v) {
    uint32_t n = 0;
    for (; v; v >>= 1) n++;
    return n;
}

SizedLit parseSized(std::string_view t) {
    SizedLit r;
    auto q = t.find('\'');
    if (q == std::string_view::npos || q == 0) return r;
    for (char c : t.substr(0, q))
        if (std::isdigit(static_cast<unsigned char>(c))) r.size = r.size * 10 + uint32_t(c - '0');
    size_t p = q + 1;
    if (p < t.size() && (t[p] == 's' || t[p] == 'S')) r.isSigned = true, p++;
    if (p >= t.size()) return r;
    char base = char(std::tolower(static_cast<unsigned char>(t[p++])));
    std::string digits;
    for (; p < t.size(); ++p)
        if (t[p] != '_' && !std::isspace(static_cast<unsigned char>(t[p])))
            digits += char(std::tolower(static_cast<unsigned char>(t[p])));
    int bpd = base == 'b' ? 1 : base == 'o' ? 3 : base == 'h' ? 4 : 0;
    if (bpd == 0) {   // decimal: accumulate the magnitude; x/z/? digits are undecidable
        r.decimal = true;
        unsigned __int128 v = 0;
        const unsigned __int128 maxv = ~static_cast<unsigned __int128>(0);
        for (char c : digits) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return r;
            unsigned d = unsigned(c - '0');
            if (v > (maxv - d) / 10) return r;   // more than 128 bits: undecidable here
            v = v * 10 + d;
        }
        uint64_t hi = uint64_t(v >> 64), lo = uint64_t(v);
        r.needed = hi ? 64 + bitLength(hi) : bitLength(lo);
        return r;
    }
    // Binary/octal/hex: the first non-zero digit fixes the length; x/z/? digits count as set bits.
    for (size_t i = 0; i < digits.size(); ++i) {
        char c = digits[i];
        unsigned dv;
        if (c == 'x' || c == 'z' || c == '?') dv = (1u << bpd) - 1;
        else if (std::isdigit(static_cast<unsigned char>(c))) dv = unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') dv = unsigned(c - 'a' + 10);
        else return r;
        if (dv) {
            r.needed = uint32_t((digits.size() - i - 1) * size_t(bpd) + bitLength(dv));
            return r;
        }
    }
    r.needed = 0;
    return r;
}

enum class Fit { Fits, Overflows, Unknown };

// Whether the digits of a sized literal fit its declared size. The text is returned for the message.
Fit sizedFit(const Lint& l, const IntegerLiteral& lit, std::string& text) {
    auto src = sourceText(l, lit.sourceRange);
    if (!src) return Fit::Unknown;
    text = std::string(*src);
    auto p = parseSized(*src);
    if (!p.needed || p.size == 0) return Fit::Unknown;
    // A signed decimal literal spends its top bit on the sign.
    uint32_t limit = p.size - ((p.decimal && p.isSigned) ? 1u : 0u);
    return *p.needed > limit ? Fit::Overflows : Fit::Fits;
}

// Shared detector for LNT-026 (outside a concatenation) and LNT-056 (inside one): the caller picks the rule id.
void reportSizedFit(Lint& l, const std::string& path, const IntegerLiteral& lit, std::string_view where) {
    std::string text;
    switch (sizedFit(l, lit, text)) {
        case Fit::Fits: break;
        case Fit::Overflows:
            reportIn(l, path, lit.sourceRange.start(),
                     "sized literal " + text + " needs more bits than its size" + std::string(where) + "; the value is truncated",
                     {.nextStep = "Widen the literal or correct its value."});
            break;
        case Fit::Unknown:
            reportIn(l, path, lit.sourceRange.start(),
                     "sized literal " + (text.empty() ? std::string("(text not available)") : text) +
                         " could not be checked against its size" + std::string(where),
                     {.confidence = Confidence::Unresolved, .nextStep = "Write the literal in the source file (not via a macro) so its digits can be checked."});
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-026: sized literals that do not fit (outside concatenations); LNT-073: constants assigned to a narrower target that change
// their value; LNT-074: unsized literals compared with a wide vector.
class LiteralSizeRule final : public LintRule {
public:
    enum Kind { Overflow, Narrowing, UnsizedCompare };
    explicit LiteralSizeRule(Kind k) : LintRule(k == Overflow ? "LNT-026" : k == Narrowing ? "LNT-073" : "LNT-074"), kind_(k) {}

private:
    Kind kind_;
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        Kind kind;
        int concat = 0;
        V(Lint& lint, Kind k) : l(lint), kind(k) {}
        void handle(const ConcatenationExpression& c) {
            concat++;
            visitDefault(c);
            concat--;
        }
        void handle(const ReplicationExpression& r) {
            concat++;
            visitDefault(r);
            concat--;
        }
        void handle(const IntegerLiteral& lit) {
            if (kind == Overflow && concat == 0 && !lit.isDeclaredUnsized && l.isUser(lit.sourceRange.start())) {
                l.count();
                reportSizedFit(l, path, lit, "");
            }
        }
        void handle(const BinaryExpression& b) {
            if (kind == UnsizedCompare && isCompare(b.op) && l.isUser(b.sourceRange.start())) {
                l.count();
                for (int side = 0; side < 2; side++) {
                    const Expression* literal = &unwrap(side == 0 ? b.left() : b.right());
                    if (literal->kind == ExpressionKind::UnaryOp && literal->as<UnaryExpression>().op == UnaryOperator::Minus)
                        literal = &unwrap(literal->as<UnaryExpression>().operand());
                    const Expression& lit = *literal;
                    const Expression& other = unwrap(side == 0 ? b.right() : b.left());
                    auto w = widthOf(other);
                    if (lit.kind == ExpressionKind::IntegerLiteral && lit.as<IntegerLiteral>().isDeclaredUnsized && w && *w > 32)
                        reportIn(l, path, b.sourceRange.start(),
                                 "unsized literal compared with a " + std::to_string(*w) +
                                     "-bit operand: the literal is " + std::to_string(lit.as<IntegerLiteral>().getValue().getBitWidth()) +
                                     " bits before extension",
                                 {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                                  .nextStep = "Use a sized literal of the operand's width (e.g. 64'h...) to make the comparison width explicit."});
                }
            }
            visitDefault(b);
        }
        void handle(const AssignmentExpression& a) {
            const Expression& rhs = unwrap(a.right());
            auto tw = widthOf(a.left());
            if (kind == Narrowing && !a.isCompound() && tw && isConst(l, rhs) && l.isUser(a.sourceRange.start())) {
                l.count();
                SVInt v = l.evalConst(rhs).integer();
                if (!v.hasUnknown() && *tw < v.getBitWidth()) {
                    // The value survives truncation if it reads back unchanged under the target's signedness or the
                    // constant's own (e.g. -1 into a 4-bit unsigned keeps its all-ones pattern and is not reported).
                    auto survives = [&](bool sgn) {
                        SVInt t = v.resize(*tw);
                        t.setSigned(sgn);
                        return bool(t.extend(v.getBitWidth(), sgn) == v);
                    };
                    bool tsigned = a.left().type && a.left().type->isSigned();
                    if (!survives(tsigned) && !survives(v.isSigned())) {
                        SVInt back = v.resize(*tw);
                        back.setSigned(tsigned);
                        reportIn(l, path, a.sourceRange.start(),
                                 "constant " + v.toString() + " does not fit the " + std::to_string(*tw) +
                                     "-bit target: the stored value is " + back.extend(v.getBitWidth(), tsigned).toString(),
                                 {.nextStep = "Use a value that fits the target, or widen the target."});
                    }
                }
            }
            visitDefault(a);
        }
    };
    void check(Lint& l) override {
        V v(l, kind_);
        l.visitHardware(v);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-034: conditional arms of different width or mixed signedness. Fill literals are width-neutral and ignored.
class ConditionalRule final : public LintRule {
public:
    ConditionalRule() : LintRule("LNT-034", "") {}

private:
    struct Arm {
        const Expression* e;   // unwrapped
        OptWidth width;
        bool fill = false, sized = false, unsized = false, unsizedDec = false, literal = false, negative = false;
        bool isSigned() const { return e->type && e->type->isSigned(); }
    };
    static Arm arm(const Expression& raw) {
        const Expression& e = unwrap(raw);
        Arm a{&e, widthOf(e)};
        if (e.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral) {
            a.fill = true;
        }
        else if (e.kind == ExpressionKind::IntegerLiteral) {
            auto& lit = e.as<IntegerLiteral>();
            a.literal = true;
            a.sized = !lit.isDeclaredUnsized;
            a.unsized = lit.isDeclaredUnsized;
            a.unsizedDec = lit.isDeclaredUnsized && lit.type->isSigned();
            a.negative = lit.getValue().isSigned() && lit.getValue().isNegative();
        }
        return a;
    }

    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        ValueRanges ranges;
        std::unordered_map<const Expression*, uint32_t> consumed;   // conditional -> width of the assignment or cast that takes its value
        explicit V(Lint& lint) : l(lint), ranges(lint) {}
        void handle(const ForLoopStatement& s) { ranges.visitLoop(*this, s); }
        void handle(const AssignmentExpression& a) {
            if (!a.isCompound())
                if (auto w = widthOf(a.left())) consume(a.right(), *w);
            visitDefault(a);
        }
        void handle(const ConversionExpression& c) {
            if (!c.isImplicit() && c.type && c.type->isIntegral()) consume(c.operand(), c.type->getBitWidth());
            visitDefault(c);
        }
        void consume(const Expression& e, uint32_t width) {
            const Expression& u = unwrap(e);
            if (u.kind == ExpressionKind::ConditionalOp) consumed[&u] = width;
        }
        void handle(const ConditionalExpression& c) {
            if (auto it = consumed.find(&c); it != consumed.end()) {   // an arm that is itself a conditional is consumed the same way
                consume(c.left(), it->second);
                consume(c.right(), it->second);
            }
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                check(c);
            }
            visitDefault(c);
        }
        // The low `bits` bits of the expression depend only on the low `bits` bits of its parts: sums, products and bitwise
        // operations of constants and of operands at least that wide. A right shift or any other operation lets upper bits flow down.
        bool lowBitsOnly(const Expression& raw, uint32_t bits) const {
            const Expression& e = unwrap(raw);
            if (isConst(l, e)) return true;
            switch (e.kind) {
                case ExpressionKind::BinaryOp: {
                    const auto& b = e.as<BinaryExpression>();
                    const bool closed = b.op == BinaryOperator::Add || b.op == BinaryOperator::Subtract || b.op == BinaryOperator::Multiply ||
                                        b.op == BinaryOperator::BinaryAnd || b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryXor;
                    return closed && lowBitsOnly(b.left(), bits) && lowBitsOnly(b.right(), bits);
                }
                case ExpressionKind::ConditionalOp:
                    return lowBitsOnly(e.as<ConditionalExpression>().left(), bits) && lowBitsOnly(e.as<ConditionalExpression>().right(), bits);
                case ExpressionKind::NamedValue:
                case ExpressionKind::HierarchicalValue:
                case ExpressionKind::ElementSelect:
                case ExpressionKind::RangeSelect:
                case ExpressionKind::MemberAccess: {
                    const auto w = widthOf(e);
                    return w && *w >= bits;
                }
                default: return false;
            }
        }
        // The wider arm is a sum or difference with a constant (`x - 1`, 32 bits only because of the unsized literal), or a conditional
        // of such arms and operands at least `bits` wide.
        bool stepArm(const Expression& raw, uint32_t bits) const {
            const Expression& e = unwrap(raw);
            if (e.kind == ExpressionKind::ConditionalOp) {
                const auto& c = e.as<ConditionalExpression>();
                const auto ok = [&](const Expression& x) {
                    const auto w = widthOf(unwrap(x));
                    return stepArm(x, bits) || (w && *w >= bits && lowBitsOnly(x, bits));
                };
                return ok(c.left()) && ok(c.right());
            }
            if (e.kind != ExpressionKind::BinaryOp) return false;
            const auto& b = e.as<BinaryExpression>();
            return (b.op == BinaryOperator::Add || b.op == BinaryOperator::Subtract) &&
                   (isConst(l, unwrap(b.left())) != isConst(l, unwrap(b.right())));
        }
        void check(const ConditionalExpression& c) {
            Arm a = arm(c.left()), b = arm(c.right());
            if (a.fill || b.fill || !a.width || !b.width) return;
            const bool wdiff = *a.width != *b.width;
            // Unsized (decimal or based) literals take a fixed 32-bit width and are left to the info case below.
            bool widthWarn = wdiff && !a.unsized && !b.unsized;
            // The consumer keeps only the low bits both arms agree on, and neither arm lets a higher bit flow into them.
            if (widthWarn)
                if (auto it = consumed.find(&c); it != consumed.end() && it->second <= std::min(*a.width, *b.width)) {
                    const Arm& wide = *a.width > *b.width ? a : b;
                    const Arm& narrow = *a.width > *b.width ? b : a;
                    if (stepArm(*wide.e, it->second) && lowBitsOnly(*wide.e, it->second) && lowBitsOnly(*narrow.e, it->second))
                        widthWarn = false;
                }
            // A signed arm that cannot be negative extends the same way as an unsigned one.
            const auto nonNegative = [&](const Arm& x) {
                const auto range = ranges.of(*x.e);
                return range && range->nonNegative();
            };
            const bool signWarn = a.isSigned() != b.isSigned() &&
                                  ((a.isSigned() && !nonNegative(a)) || (b.isSigned() && !nonNegative(b)));
            if (widthWarn || signWarn) {
                std::string msg = widthWarn ? "conditional arms differ in width (" + std::to_string(*a.width) + " and " +
                                                  std::to_string(*b.width) + " bits)"
                                            : "conditional mixes signed and unsigned arms";
                if (widthWarn && signWarn) msg += " and signedness";
                reportIn(l, path, c.sourceRange.start(), msg + ": the result takes the wider or unsigned type of both arms",
                         {.nextStep = "Make both arms the same width and signedness (cast the narrower arm explicitly)."});
                return;
            }
            // An unsized decimal literal (32-bit signed) against a non-literal arm wider than 32 bits.
            const Arm* lit = a.unsizedDec ? &a : b.unsizedDec ? &b : nullptr;
            const Arm* other = lit == &a ? &b : &a;
            if (lit && !other->literal && *other->width > 32)
                reportIn(l, path, c.sourceRange.start(),
                         "unsized decimal literal arm against a " + std::to_string(*other->width) +
                             "-bit arm: the literal is 32-bit signed and is extended to the wider arm",
                         {.severity = Severity::Info, .nextStep = "Write the literal with an explicit size."});
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// The value-range walk the arithmetic and shift rules share: the instance path, `for` loop variables with their ranges, and a branch
// condition that compares a variable with a constant (also through a net assigned such a comparison) bounds the variable inside the
// branch, unless the procedure writes it by a blocking assignment: then the value the condition tested is not the value the branch
// reads.
template <typename Derived>
struct RangedVisitor : HardwareVisitor<Derived, VisitFlags::AllCanonical> {
    std::string path;   // hierarchical path of the instance body being visited
    Lint& l;
    ValueRanges ranges;
    lint::BlockingWrites writes;
    explicit RangedVisitor(Lint& lint) : l(lint), ranges(lint) {}
    void handle(const InstanceBodySymbol& b) {
        auto saved = path;
        path = b.getHierarchicalPath();
        this->visitDefault(b);
        path = saved;
    }
    void handle(const ForLoopStatement& loop) { ranges.visitLoop(*static_cast<Derived*>(this), loop); }
    void handle(const ProceduralBlockSymbol& p) {
        auto outer = std::move(writes);
        writes = lint::blockingWritesIn(p.getBody());
        this->visitDefault(p);
        writes = std::move(outer);
    }
    void handle(const SubroutineSymbol& r) {
        auto outer = std::move(writes);
        writes = lint::blockingWritesIn(r.getBody());
        this->visitDefault(r);
        writes = std::move(outer);
    }
    std::vector<ValueRanges::Guard> guards(const Expression& cond, bool truth) const {
        if (writes.unknown) return {};
        auto found = ranges.guardsOf(cond, truth);
        std::erase_if(found, [&](const ValueRanges::Guard& g) { return writes.symbols.count(g.var) != 0; });
        return found;
    }
    void handle(const ConditionalStatement& s) {
        if (s.conditions.size() != 1 || s.conditions[0].pattern) { this->visitDefault(s); return; }
        const Expression& cond = *s.conditions[0].expr;
        cond.visit(*static_cast<Derived*>(this));
        { ValueRanges::Narrowing n(ranges, guards(cond, true)); s.ifTrue.visit(*static_cast<Derived*>(this)); }
        if (s.ifFalse) { ValueRanges::Narrowing n(ranges, guards(cond, false)); s.ifFalse->visit(*static_cast<Derived*>(this)); }
    }
    void handle(const ConditionalExpression& c) {
        if (c.conditions.size() != 1 || c.conditions[0].pattern) { this->visitDefault(c); return; }
        const Expression& cond = *c.conditions[0].expr;
        cond.visit(*static_cast<Derived*>(this));
        { ValueRanges::Narrowing n(ranges, guards(cond, true)); c.left().visit(*static_cast<Derived*>(this)); }
        { ValueRanges::Narrowing n(ranges, guards(cond, false)); c.right().visit(*static_cast<Derived*>(this)); }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-035: unsized literal shifted in a self-determined position that is wider than 32 bits, and signed left shifts
// of non-constant operands whose context widens them.
class UnsizedShiftRule final : public LintRule {
public:
    UnsizedShiftRule() : LintRule("LNT-035", "", true) {}

private:
    struct V : RangedVisitor<V> {
        using RangedVisitor<V>::handle;
        OptWidth target;   // width of the innermost enclosing assignment target
        explicit V(Lint& lint) : RangedVisitor<V>(lint) {}
        void handle(const AssignmentExpression& a) {
            auto saved = target;
            target = widthOf(a.left());
            visitDefault(a);
            target = saved;
        }
        void handle(const ConcatenationExpression& c) {
            for (auto* op : c.operands()) selfDetermined(*op, widthOf(c));
            visitDefault(c);
        }
        void handle(const ReplicationExpression& r) {
            selfDetermined(r.concat(), widthOf(r));
            visitDefault(r);
        }
        // A select index is self-determined: a shift in it keeps its own width.
        void handle(const ElementSelectExpression& e) {
            lostBits(e.selector(), std::nullopt);
            visitDefault(e);
        }
        // A left shift evaluated at the width of its own left operand drops the bits its count pushes past that width. In a position the
        // context does not widen (a concatenation or replication operand, a select index, a comparison operand the other side does not
        // widen) that happens where the author may have expected the wider result: with a count whose range reaches the width,
        // a WARNING; with a count whose range is not known, an obligation; with a count the range keeps below the width, nothing.
        // `container` is the width of the word the shift is placed in, when it is wider than the shift.
        void lostBits(const Expression& operand, OptWidth container) {
            const Expression& e = unwrap(operand);
            if (e.kind != ExpressionKind::BinaryOp || !l.isUser(e.sourceRange.start())) return;
            const auto& shift = e.as<BinaryExpression>();
            if (!isLeftShift(shift.op) || isConst(l, shift.right())) return;
            const auto own = widthOf(e), left = widthOf(unwrap(shift.left()));
            if (!own || !left || *own != *left || (container && *container <= *own)) return;
            const auto count = ranges.of(shift.right());
            if (count && count->hi < static_cast<Wide>(*own)) return;
            l.count();
            if (count)
                reportIn(l, path, e.sourceRange.start(),
                         "left shift is evaluated at " + std::to_string(*own) + " bits, and its count can reach " + toString(count->hi) +
                             ": every bit it pushes past bit " + std::to_string(*own - 1) + " is lost" +
                             (container ? ", though the result is placed in a " + std::to_string(*container) + "-bit word" : std::string()),
                         {.confidence = Confidence::Heuristic,
                          .nextStep = "Widen the operand before shifting (a size cast or a wider operand) if the shifted-out bits are needed.",
                          .identityKey = "lost-shift:" + std::to_string(*own)});
            else
                reportIn(l, path, e.sourceRange.start(),
                         "left shift is evaluated at " + std::to_string(*own) + " bits and the range of its count is not known: whether it can push bits past bit " +
                             std::to_string(*own - 1) + " is not decided",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Constrain the count in the RTL, or widen the operand before shifting.",
                          .identityKey = "lost-shift-unknown:" + std::to_string(*own)});
        }
        // Operands of a concatenation are self-determined: an unsized left operand of a shift stays 32 bits there.
        void selfDetermined(const Expression& op, OptWidth container) {
            if (!container || op.kind != ExpressionKind::BinaryOp) return;
            auto& s = op.as<BinaryExpression>();
            if (!isLeftShift(s.op)) return;
            const Expression& lit = unwrap(s.left());
            auto own = widthOf(op);
            if (lit.kind != ExpressionKind::IntegerLiteral || !lit.as<IntegerLiteral>().isDeclaredUnsized) { lostBits(op, container); return; }
            if (!own || *own > 32) return;
            uint32_t ctx = std::max<uint32_t>(*container, target.value_or(0));
            if (ctx > 32 && l.isUser(op.sourceRange.start())) {
                l.count();
                reportIn(l, path, op.sourceRange.start(),
                         "unsized literal is 32 bits in this self-determined shift; shift amounts of 32 or more are lost before the result is widened to " +
                             std::to_string(ctx) + " bits",
                         {.confidence = Confidence::Heuristic,
                          .nextStep = "Size the literal to the result width, e.g. 64'd1 << n."});
            }
        }
        void handle(const BinaryExpression& b) {
            if (isCompare(b.op)) { lostBits(b.left(), std::nullopt); lostBits(b.right(), std::nullopt); }
            if (isLeftShift(b.op) && l.isUser(b.sourceRange.start())) {
                const Expression& lhs = unwrap(b.left());
                auto lw = widthOf(lhs), ew = widthOf(b);
                if (lhs.kind != ExpressionKind::IntegerLiteral && !isConst(l, lhs) && lhs.type && lhs.type->isSigned() &&
                    lw && ew && *ew > *lw && !isConst(l, b.right())) {
                    l.count();
                    reportIn(l, path, b.sourceRange.start(),
                             "signed operand of a left shift by a variable amount is widened to " + std::to_string(*ew) +
                                 " bits before shifting: the bits shifted into the sign position differ from the " +
                                 std::to_string(*lw) + "-bit result",
                             {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                              .nextStep = "Shift in the operand's own width, or make the widening explicit with a cast.",
                              .identityKey = std::to_string(*lw) + ":" + std::to_string(*ew)});
                }
            }
            visitDefault(b);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-036: multiplication whose context is narrower than its operands, carry loss of a +/- inside a concatenation,
// and (report_modular=true) +/- assigned at the operand width.
struct ValueRange { int64_t min = 0, max = 0; };

class OperandRanges {
public:
    // `allowWrap`: a counter that wraps in its own type still runs its leading iterations, which are certain (LNT-006 only).
    void enter(const ForLoopStatement& loop, Lint& l, bool allowWrap = false) {
        if (auto values = lint::constantLoopValues(loop, l, allowWrap)) {
            loops_[values->var] = {values->min(), values->max()};
            if (values->wraps) wrapping_.insert(values->var);
        }
    }
    /// True when `e` names a loop variable that wraps: its range covers only the leading iterations.
    bool wraps(const Expression& e) const {
        bool hit = false;
        e.visitSymbolReferences([&](const Expression&, const Symbol& sym) { hit = hit || wrapping_.count(static_cast<const ValueSymbol*>(&sym)) != 0; });
        return hit;
    }
    void leave(const ForLoopStatement& loop) {
        for (auto* v : loop.loopVars) { loops_.erase(v); wrapping_.erase(v); }
        if (loop.stopExpr && loop.stopExpr->kind == ExpressionKind::BinaryOp) {
            const auto& left = unwrap(loop.stopExpr->as<BinaryExpression>().left());
            if (left.kind == ExpressionKind::NamedValue) { loops_.erase(&left.as<NamedValueExpression>().symbol); wrapping_.erase(&left.as<NamedValueExpression>().symbol); }
        }
    }
    /// The range of `e` with implicit conversions removed, or nothing for a non-integral or very wide operand.
    std::optional<ValueRange> of(const Expression& e) const {
        const Expression& operand = unwrap(e);
        if (operand.kind == ExpressionKind::NamedValue)
            if (auto it = loops_.find(&operand.as<NamedValueExpression>().symbol); it != loops_.end()) return it->second;
        const Type& type = *operand.type;
        if (!type.isIntegral()) return std::nullopt;
        const auto width = type.getBitWidth();
        if (width == 0 || width > 62) return std::nullopt;
        if (type.isSigned()) return ValueRange{-(int64_t{1} << (width - 1)), (int64_t{1} << (width - 1)) - 1};
        return ValueRange{0, (int64_t{1} << width) - 1};
    }
    /// The loop variable `enter` bounded for this loop, or nothing.
    const ValueSymbol* boundedVar(const ForLoopStatement& loop) const {
        if (!loop.stopExpr || loop.stopExpr->kind != ExpressionKind::BinaryOp) return nullptr;
        const auto& left = unwrap(loop.stopExpr->as<BinaryExpression>().left());
        if (left.kind != ExpressionKind::NamedValue) return nullptr;
        const auto* var = &left.as<NamedValueExpression>().symbol;
        return loops_.count(var) ? var : nullptr;
    }
    static std::optional<int64_t> constant(Lint& l, const Expression& e) { return lint::constantInt(l, e); }

private:
    std::unordered_map<const ValueSymbol*, ValueRange> loops_;
    std::unordered_set<const ValueSymbol*> wrapping_;
};

class ArithmeticRule final : public LintRule {
public:
    ArithmeticRule() : LintRule("LNT-036") {}

private:
    struct V : RangedVisitor<V> {
        using RangedVisitor<V>::handle;
        bool modular = false;
        std::unordered_set<const Expression*> carryReported;   // sums and shifts already named by lostCarry
        explicit V(Lint& lint) : RangedVisitor<V>(lint) {}

        // A sum or left shift of non-constant operands whose every possible value lies beyond what its evaluated width holds: whatever the
        // inputs are, the result has wrapped (`{1'b1, a[6:0]} + {1'b1, b[6:0]}` in 8 bits is always at least 256). The value range is a
        // bound that never excludes a reachable value, so a lower bound above the maximum (or an upper bound below the minimum) proves it.
        void alwaysWraps(const BinaryExpression& b) {
            if (b.op != BinaryOperator::Add && !isLeftShift(b.op)) return;
            if (!l.isUser(b.sourceRange.start()) || !b.type || carryReported.count(&b)) return;
            if (isConst(l, unwrap(b.left())) && isConst(l, unwrap(b.right()))) return;
            const auto math = ranges.mathematical(b);
            const auto full = lint::typeRange(*b.type);
            if (!math || !full || (math->lo <= full->hi && math->hi >= full->lo)) return;
            const auto width = widthOf(b);
            if (!width) return;
            l.count();
            reportIn(l, path, b.sourceRange.start(),
                     std::string(isLeftShift(b.op) ? "shifted value" : "sum") + " is evaluated at " + std::to_string(*width) +
                         " bits but always lies beyond them (its value is " + lint::toString(*math) + "): the result has wrapped for every input",
                     {.confidence = Confidence::Heuristic,
                      .nextStep = "Widen an operand before the operation (e.g. {1'b0, a} + {1'b0, b}) if the full value is needed.",
                      .identityKey = std::string("always-wraps:") + std::to_string(*width)});
        }

        std::optional<bool> shiftCountWraps(const BinaryExpression& shift, std::string& detail) const {
            const Expression& count = unwrap(shift.right());
            if (count.kind != ExpressionKind::BinaryOp) return false;
            const auto& arith = count.as<BinaryExpression>();
            if (arith.op != BinaryOperator::Add && arith.op != BinaryOperator::Subtract) return false;
            auto sameNamed = [](const Expression& a, const Expression& b) {
                const auto& x = unwrap(a);
                const auto& y = unwrap(b);
                const auto named = [](const Expression& e) {
                    return e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue;
                };
                return named(x) && named(y) && x.getSymbolReference() == y.getSymbolReference();
            };
            if (arith.op == BinaryOperator::Subtract && sameNamed(arith.left(), arith.right())) return false;
            if (arith.op == BinaryOperator::Add) {
                const auto complement = [&](const Expression& a, const Expression& b) {
                    const auto& u = unwrap(b);
                    return u.kind == ExpressionKind::UnaryOp && u.as<UnaryExpression>().op == UnaryOperator::BitwiseNot &&
                           sameNamed(a, u.as<UnaryExpression>().operand()) &&
                           a.type->getBitWidth() == u.type->getBitWidth() && a.type->isSigned() == u.type->isSigned();
                };
                if (complement(arith.left(), arith.right()) || complement(arith.right(), arith.left())) return false;
            }
            auto width = widthOf(count);
            detail = "self-determined shift-count arithmetic exceeds the supported range model; whether it wraps is unresolved";
            if (!width || *width == 0 || *width > 62) return std::nullopt;
            auto left = ranges.of(arith.left()), right = ranges.of(arith.right());
            if (!left || !right) return std::nullopt;
            __int128 lo, hi;
            if (arith.op == BinaryOperator::Add) {
                lo = left->lo + right->lo;
                hi = left->hi + right->hi;
            } else {
                lo = left->lo - right->hi;
                hi = left->hi - right->lo;
            }
            bool sign = count.type && count.type->isSigned();
            int64_t representableMin = sign ? -(int64_t{1} << (*width - 1)) : 0;
            int64_t representableMax = sign ? (int64_t{1} << (*width - 1)) - 1 : (int64_t{1} << *width) - 1;
            if (lo >= representableMin && hi <= representableMax) return false;
            detail = "self-determined shift-count arithmetic can exceed its " + std::to_string(*width) +
                     "-bit result range, so the shift amount can wrap before the shift";
            return true;
        }
        // A sum, difference or left shift evaluated at its operands' own width (the context does not widen it) whose value range exceeds
        // that width, and whose result a right shift, a division or a comparison then uses: the carry the next operator would
        // use is already lost. Products are LNT-036 (a); an operand that is a constant (`cnt + 1`) is modular arithmetic by intent.
        // An unsigned difference is modular on purpose (the wrap-safe timer `(now - start) >= timeout`, a pointer difference): only a sum or a
        // left shift loses a carry, but a sum or left shift beneath it is its own site. A sum compared with one of its own unsigned operands (`(a + b) < a`,
        // also through a same-width cast) is the overflow test itself. A constant addend does not hide the sum beneath it: the chain's whole range decides.
        static const Expression& sameWidthOperand(const Expression& e) {
            const Expression* x = &unwrap(e);
            while (x->kind == ExpressionKind::Conversion) {
                const Expression& inner = unwrap(x->as<ConversionExpression>().operand());
                if (!inner.type || !x->type || !inner.type->isIntegral() || !x->type->isIntegral() ||
                    inner.type->getBitWidth() != x->type->getBitWidth() || inner.type->isSigned() != x->type->isSigned())
                    break;
                x = &inner;
            }
            return *x;
        }
        static bool sameSignal(const Expression& a, const Expression& b) {
            const auto& x = sameWidthOperand(a);
            const auto& y = sameWidthOperand(b);
            const auto named = [](const Expression& e) { return e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue; };
            return named(x) && named(y) && x.getSymbolReference() == y.getSymbolReference();
        }
        // `chain` is the outermost sum of a chain with constant addends: the carry is judged on the value range of the whole chain.
        void lostCarry(const Expression& operand, const char* use, const Expression* other = nullptr, const Expression* chain = nullptr) {
            const Expression& e = unwrap(operand);
            if (e.kind != ExpressionKind::BinaryOp || !l.isUser(e.sourceRange.start())) return;
            const auto& op = e.as<BinaryExpression>();
            const bool shift = isLeftShift(op.op);
            if (op.op != BinaryOperator::Add && op.op != BinaryOperator::Subtract && !shift) return;
            if (op.op == BinaryOperator::Subtract && e.type && !e.type->isSigned()) {
                // the difference itself is modular on purpose; a sum or left shift beneath it is its own site
                lostCarry(op.left(), use, nullptr, chain);
                lostCarry(op.right(), use, nullptr, chain);
                return;
            }
            if (other && op.op == BinaryOperator::Add && e.type && !e.type->isSigned() &&
                (sameSignal(op.left(), *other) || sameSignal(op.right(), *other)))
                return;
            if (isConst(l, unwrap(op.left()))) {
                if (op.op == BinaryOperator::Add) lostCarry(op.right(), use, nullptr, chain ? chain : &e);
                return;
            }
            // a shift by a run-time count is LNT-035's, except the own-operand compare `(a << s) < a`; a sum or shift beneath it is its own site
            if (shift && !isConst(l, unwrap(op.right())) && !(other && sameSignal(op.left(), *other))) {
                lostCarry(op.left(), "shifted left", nullptr, chain);
                return;
            }
            if (!shift && isConst(l, unwrap(op.right()))) {
                // a constant addend is modular by intent for this sum, but it does not hide a sum or shift beneath it
                lostCarry(op.left(), use, nullptr, chain ? chain : &e);
                return;
            }
            const auto own = widthOf(e), left = widthOf(unwrap(op.left()));
            const auto right = shift ? left : widthOf(unwrap(op.right()));
            if (!own || !left || !right || *own != std::max(*left, *right) || ranges.wrapFree(chain ? *chain : e)) return;
            l.count();
            carryReported.insert(&e);
            reportIn(l, path, e.sourceRange.start(),
                     std::string(shift ? "shifted value" : op.op == BinaryOperator::Add ? "sum" : "difference") + " is evaluated at " + std::to_string(*own) +
                         " bits, so its carry bit is lost before it is " + use,
                     {.confidence = Confidence::Heuristic,
                      .nextStep = "Widen an operand before the operation (e.g. {1'b0, a} + {1'b0, b}) if the carry is needed.",
                      .identityKey = std::string("lost-carry:") + use + ":" + std::to_string(*own)});
        }
        void handle(const BinaryExpression& b) {
            if (l.isUser(b.sourceRange.start())) {
                if (b.op == BinaryOperator::LogicalShiftRight || b.op == BinaryOperator::ArithmeticShiftRight) lostCarry(b.left(), "shifted right");
                else if (b.op == BinaryOperator::Divide) lostCarry(b.left(), "divided");
                else if (isCompare(b.op)) { lostCarry(b.left(), "compared", &b.right()); lostCarry(b.right(), "compared", &b.left()); }
                if (b.op == BinaryOperator::Multiply) product(b);
                if (isLeftShift(b.op) || b.op == BinaryOperator::LogicalShiftRight || b.op == BinaryOperator::ArithmeticShiftRight) {
                    std::string detail;
                    const auto wraps = shiftCountWraps(b, detail);
                    if (!wraps || *wraps) {
                        l.count();
                        reportIn(l, path, b.right().sourceRange.start(), detail,
                                 {.confidence = wraps ? Confidence::Heuristic : Confidence::Unresolved,
                                  .proof = wraps ? ProofState::NotApplicable : ProofState::NotAttempted,
                                  .nextStep = "Widen the shift-count arithmetic before it is evaluated, or constrain its inputs in the RTL.",
                                  .identityKey = "shift-count-wrap:" + std::to_string(widthOf(unwrap(b.right())).value_or(0))});
                    }
                }
            }
            alwaysWraps(b);
            visitDefault(b);
        }
        void product(const BinaryExpression& b) {
            auto ew = widthOf(b);
            auto lw = operandBits(l, unwrap(b.left()));
            auto rw = operandBits(l, unwrap(b.right()));
            if (!ew || !lw || !rw) return;
            l.count();
            // The operand ranges (a loop variable's, a narrow unsigned operand's, a constant) keep the product inside its width.
            if (*ew < *lw + *rw && !ranges.wrapFree(b))
                reportIn(l, path, b.sourceRange.start(),
                         "product truncated: " + std::to_string(*lw) + "-bit and " + std::to_string(*rw) +
                             "-bit operands are evaluated at " + std::to_string(*ew) + " bits, so the upper bits are lost",
                         {.confidence = Confidence::Heuristic,
                          .nextStep = "Widen the target or cast one operand to the full product width if the full product is intended.",
                          .identityKey = std::to_string(*lw) + ":" + std::to_string(*rw) + ":" + std::to_string(*ew)});
        }
        // The sum is the least significant operand and everything above it is constant, at least as wide as the sum: a zero-extension of
        // a modular sum into a wider word (`{32'b0, a + b}`), not a carry slot (`{1'b0, a + b}` stays reported).
        static bool zeroExtendsSum(Lint& l, std::span<const Expression* const> operands, size_t at, uint32_t sumWidth) {
            if (at + 1 != operands.size() || at == 0) return false;
            uint64_t above = 0;
            for (size_t i = 0; i < at; ++i) {
                if (!isConst(l, unwrap(*operands[i]))) return false;
                above += widthOf(*operands[i]).value_or(0);
            }
            return above >= sumWidth;
        }
        void handle(const ConcatenationExpression& c) {
            const auto operands = c.operands();
            for (size_t index = 0; index < operands.size(); ++index) {
                const auto* op = operands[index];
                if (op->kind != ExpressionKind::BinaryOp) continue;
                auto& s = op->as<BinaryExpression>();
                if (s.op != BinaryOperator::Add && s.op != BinaryOperator::Subtract) continue;
                if (!l.isUser(s.sourceRange.start())) continue;
                auto ew = widthOf(*op);
                auto lw = widthOf(unwrap(s.left())), rw = widthOf(unwrap(s.right()));
                if (!ew || !lw || !rw || isConst(l, unwrap(s.left())) || isConst(l, unwrap(s.right()))) continue;
                // A constant word at least as wide sits above the sum: a modular sum zero-extended, so the dropped carry is likely intended.
                const bool padded = zeroExtendsSum(l, operands, index, *ew);
                l.count();
                if (*ew == std::max(*lw, *rw) && !ranges.wrapFree(s)) {
                    if (padded)
                        reportIn(l, path, s.sourceRange.start(),
                                 "the carry of the sum is dropped by the concatenation; intended for a modular sum",
                                 {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                                  .nextStep = "Widen an operand before the concatenation if the carry is needed; otherwise nothing to do."});
                    else
                        reportIn(l, path, s.sourceRange.start(),
                                 "sum inside a concatenation is evaluated at " + std::to_string(*ew) +
                                     " bits, so its carry bit is lost",
                                 {.confidence = Confidence::Heuristic,
                                  .nextStep = "Widen an operand before the concatenation (e.g. {1'b0, a} + {1'b0, b}) if the carry is needed."});
                }
            }
            visitDefault(c);
        }
        void handle(const AssignmentExpression& a) {
            if (modular && !a.isCompound() && l.isUser(a.sourceRange.start())) {
                const Expression& r = unwrap(a.right());
                auto lw = widthOf(a.left());
                if (r.kind == ExpressionKind::BinaryOp && lw) {
                    auto& s = r.as<BinaryExpression>();
                    auto ew = widthOf(r);
                    auto ow = widthOf(unwrap(s.left())), rw = widthOf(unwrap(s.right()));
                    if ((s.op == BinaryOperator::Add || s.op == BinaryOperator::Subtract) && ew && ow && rw && *ew == *lw &&
                        *ow == *ew && *rw == *ew && !isConst(l, unwrap(s.left())) && !isConst(l, unwrap(s.right()))) {
                        l.count();
                        reportIn(l, path, a.sourceRange.start(),
                                 "sum of two " + std::to_string(*ew) +
                                     "-bit operands is assigned to a " + std::to_string(*lw) +
                                     "-bit target: the carry is discarded (modular arithmetic)",
                                 {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                                  .nextStep = "Confirm wraparound is intended; widen the target if the carry matters."});
                    }
                }
            }
            visitDefault(a);
        }
    };
    void check(Lint& l) override {
        V v(l);
        v.modular = l.option("report_modular", "false") == "true";
        l.visitHardware(v);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-055: an unsigned difference compared below zero, `(a - b) < 0`: the subtraction wraps, so the comparison is never true.
// Comparisons of a plain variable with a constant outside its range (counter guards, `x < 0` after a decrement) are LNT-038.
class WraparoundRule final : public LintRule {
public:
    WraparoundRule() : LintRule("LNT-055") {}

private:
    static bool allBits(const SVInt& v, bool one) {
        if (v.hasUnknown()) return false;
        for (uint32_t i = 0; i < v.getBitWidth(); i++)
            if (bool(v[int32_t(i)]) != one) return false;
        return v.getBitWidth() > 0;
    }

    // Rebind the original syntax without comparison context; never mutate the shared AST.
    static ConstantValue selfBound(Lint& l, const Expression& e, const Scope& scope) {
        if (!e.syntax) return {};
        ASTContext context(scope, LookupLocation::max);
        return l.evalConst(Expression::bind(*e.syntax, context));
    }

    static bool isNegativeOne(Lint& l, const Expression& e, const Scope& scope) {
        const auto bound = selfBound(l, e, scope);
        return bound.isInteger() && bound.integer().isSigned() && allBits(bound.integer(), true);
    }

    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        const Scope* scope = nullptr;
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            const auto* savedScope = scope;
            scope = &b;
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
            scope = savedScope;
        }
        template<std::derived_from<Symbol> T>
        void handle(const T& symbol) {
            const auto* saved = scope;
            if constexpr (std::is_base_of_v<Scope, T>) scope = &symbol;
            visitDefault(symbol);
            scope = saved;
        }
        void handle(const BlockStatement& block) {
            const auto* saved = scope;
            if (block.blockSymbol) scope = block.blockSymbol;
            visitDefault(block);
            scope = saved;
        }
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const BinaryExpression& b) {
            if (isCompare(b.op) && l.isUser(b.sourceRange.start())) {
                const Expression& lhs = unwrap(b.left());
                const Expression& rhs = unwrap(b.right());
                if (lhs.kind == ExpressionKind::BinaryOp && lhs.type && !lhs.type->isSigned() &&
                    lhs.as<BinaryExpression>().op == BinaryOperator::Subtract &&
                    ((b.op == BinaryOperator::LessThan && isConst(l, rhs) && allBits(l.evalConst(rhs).integer(), false)) ||
                     (b.op == BinaryOperator::LessThanEqual && isConst(l, rhs) &&
                      scope && isNegativeOne(l, rhs, *scope)))) {
                    // Read the bound after comparison conversions: unsigned subtraction is in [0, all-ones].
                    const auto compared = l.evalConst(b.right());
                    const bool upperBound = b.op == BinaryOperator::LessThanEqual;
                    if (!compared.isInteger() || !allBits(compared.integer(), upperBound)) {
                        visitDefault(b);
                        return;
                    }
                    const bool result = allBits(compared.integer(), true);
                    l.count();
                    reportIn(l, path, b.sourceRange.start(),
                             std::string("unsigned difference compared below zero: the subtraction wraps and the comparison is always ") +
                                 (result ? "true" : "false"),
                             {.nextStep = "Compare the operands before subtracting, or use a signed type for the difference."});
                }
            }
            visitDefault(b);
        }
    };

    void check(Lint& l) override {
        V v(l);
        l.visitHardware(v);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-056: constant concatenations/replications whose width differs from the assignment target, and sized literals
// that overflow their size inside a concatenation or replication.
class ConstantConcatRule final : public LintRule {
public:
    ConstantConcatRule() : LintRule("LNT-056", "", true) {}

private:
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        int concat = 0;
        explicit V(Lint& lint) : l(lint) {}
        void assignment(const Expression& raw, bitwidth_t tw) {
            const Expression& rhs = unwrap(raw);
            if ((rhs.kind == ExpressionKind::Concatenation || rhs.kind == ExpressionKind::Replication) &&
                l.isUser(rhs.sourceRange.start())) {
                auto rw = widthOf(rhs);
                if (rw && isConst(l, rhs)) {
                    l.count();
                    if (*rw != tw)
                        reportIn(l, path, rhs.sourceRange.start(),
                                 "constant concatenation is " + std::to_string(*rw) + " bits but the target is " +
                                     std::to_string(tw) + " bits",
                                 {.nextStep = "Pad or trim the constant so its width matches the target."});
                }
            }
        }
        void handle(const AssignmentExpression& a) {
            if (auto tw = widthOf(a.left()); tw && !a.isCompound()) assignment(a.right(), *tw);
            visitDefault(a);
        }
        void handle(const VariableSymbol& v) {
            if (v.getType().isIntegral())
                if (auto* init = v.getInitializer()) assignment(*init, v.getType().getBitWidth());
            visitDefault(v);
        }
        void handle(const NetSymbol& n) {
            if (n.getType().isIntegral())
                if (auto* init = n.getInitializer()) assignment(*init, n.getType().getBitWidth());
            visitDefault(n);
        }
        void handle(const ConcatenationExpression& c) {
            concat++;
            visitDefault(c);
            concat--;
        }
        void handle(const ReplicationExpression& r) {
            concat++;
            visitDefault(r);
            concat--;
        }
        void handle(const IntegerLiteral& lit) {
            if (concat > 0 && !lit.isDeclaredUnsized && l.isUser(lit.sourceRange.start())) {
                l.count();
                reportSizedFit(l, path, lit, " inside a concatenation");
            }
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-057: truncation at an assignment or port connection where the width of either side comes from a parameter.
class ParamTruncationRule final : public LintRule {
public:
    ParamTruncationRule() : LintRule("LNT-057") {}

private:
    // A missing declaration, unsupported symbol or cycle cannot prove a fixed width.
    enum class Dependency { Fixed, Undecided, Dependent };
    static Dependency combine(Dependency a, Dependency b) {
        return static_cast<Dependency>(std::max(static_cast<int>(a), static_cast<int>(b)));
    }

    struct Classifier {
        unsigned depth = 0;
        std::unordered_set<const Symbol*> visiting;

        Dependency symbol(const Symbol& s) {
            if (depth >= 32 || !visiting.insert(&s).second) return Dependency::Undecided;
            ++depth;
            Dependency result = Dependency::Undecided;
            if (s.kind == SymbolKind::Parameter) {
                auto& p = s.as<ParameterSymbol>();
                if (p.isFromGenvar()) result = Dependency::Fixed;
                else if (!p.isLocalParam() && s.getParentScope() &&
                         s.getParentScope()->asSymbol().kind != SymbolKind::Package) result = Dependency::Dependent;
                else {
                    const auto* init = p.getDeclaredType()->getInitializerSyntax();
                    result = syntax(init ? static_cast<const syntax::SyntaxNode*>(init) : p.defaultValSyntax,
                                    s.getParentScope());
                }
            }
            else if (s.kind == SymbolKind::TypeParameter) {
                auto& p = s.as<TypeParameterSymbol>();
                result = p.isLocalParam() ? declared(p.targetType, s.getParentScope()) : Dependency::Dependent;
            }
            else if (s.kind == SymbolKind::TypeAlias) {
                auto& alias = s.as<TypeAliasType>();
                const Scope* scope = s.getParentScope();
                const Symbol* original = scope ? scope->find(s.name) : nullptr;
                if (original && original->kind == SymbolKind::TypeParameter &&
                    &original->as<TypeParameterSymbol>().getTypeAlias() == &alias)
                    result = symbol(*original);
                else
                    result = declared(alias.targetType, scope);
            }
            else if (s.kind == SymbolKind::ExplicitImport) {
                auto* imported = s.as<ExplicitImportSymbol>().importedSymbol();
                result = imported ? symbol(*imported) : Dependency::Undecided;
            }
            else if (s.kind == SymbolKind::EnumValue) {
                auto* decl = s.getSyntax();
                if (decl && decl->kind == syntax::SyntaxKind::Declarator) {
                    auto* init = decl->as<syntax::DeclaratorSyntax>().initializer;
                    if (init) result = syntax(init, s.getParentScope());
                    else if (auto* parent = s.getParentScope(); parent && parent->asSymbol().kind == SymbolKind::EnumType) {
                        const Symbol* previous = nullptr;
                        for (auto& item : parent->asSymbol().as<EnumType>().values()) {
                            if (&item == &s) { result = previous ? symbol(*previous) : Dependency::Fixed; break; }
                            previous = &item;
                        }
                    }
                }
            }
            --depth;
            visiting.erase(&s);
            return result;
        }

        Dependency declared(const DeclaredType& dt, const Scope* scope) {
            return syntax(dt.getTypeSyntax(), scope);
        }

        // A function is a constant function of its arguments when its body names only its own formals and locals, parameters and
        // enumerators, a few pure system functions and other such functions: its result then depends on exactly what those depend on,
        // as `$clog2(N)` depends on N, and each specialisation of the design evaluates it afresh. Anything else (a variable or net outside
        // the function, a hierarchical name, a task, a DPI import, a method, recursion) is undecided.
        Dependency function(const SubroutineSymbol& sub) {
            if (depth >= 32 || sub.subroutineKind != SubroutineKind::Function || sub.thisVar || sub.flags.has(MethodFlags::DPIImport) ||
                sub.getBody().bad() || !visiting.insert(&sub).second)
                return Dependency::Undecided;
            ++depth;
            struct Body : ASTVisitor<Body, VisitFlags::AllGood> {
                Classifier& classifier;
                const SubroutineSymbol& sub;
                Dependency result = Dependency::Fixed;
                Body(Classifier& c, const SubroutineSymbol& s) : classifier(c), sub(s) {}
                bool inside(const Symbol& s) const {
                    for (const Scope* scope = s.getParentScope(); scope; scope = scope->asSymbol().getParentScope())
                        if (&scope->asSymbol() == &sub) return true;
                    return false;
                }
                void handle(const NamedValueExpression& e) {
                    const Symbol& s = e.symbol;
                    if (inside(s)) return;
                    result = combine(result, s.kind == SymbolKind::Parameter || s.kind == SymbolKind::EnumValue ? classifier.symbol(s)
                                                                                                                : Dependency::Undecided);
                }
                void handle(const HierarchicalValueExpression&) { result = combine(result, Dependency::Undecided); }
                void handle(const CallExpression& call) {
                    if (call.isSystemCall()) {
                        static const std::set<std::string, std::less<>> pure{"$clog2", "$bits", "$signed", "$unsigned", "$countones", "$min", "$max", "$abs"};
                        if (!pure.count(call.getSubroutineName())) result = combine(result, Dependency::Undecided);
                    }
                    else if (const auto* callee = std::get_if<const SubroutineSymbol*>(&call.subroutine); callee && *callee && !call.thisClass())
                        result = combine(result, classifier.function(**callee));
                    else
                        result = combine(result, Dependency::Undecided);
                    visitDefault(call);
                }
            } body(*this, sub);
            sub.getBody().visit(body);
            --depth;
            visiting.erase(&sub);
            return body.result;
        }

        struct Scan : syntax::SyntaxVisitor<Scan> {
            Classifier& classifier;
            const Scope& scope;
            Dependency result = Dependency::Fixed;
            Scan(Classifier& classifier, const Scope& scope) : classifier(classifier), scope(scope) {}
            void name(const syntax::NameSyntax& node, bool type = false) {
                syntax::SyntaxPrinter printer;
                printer.setIncludeTrivia(false).setExpandMacros(true).print(node);
                const Symbol* s = scope.lookupName(printer.str(), LookupLocation::max,
                                                   type ? LookupFlags::Type : LookupFlags::None);
                result = combine(result, s ? classifier.symbol(*s) : Dependency::Undecided);
            }
            void handle(const syntax::NamedTypeSyntax& node) { name(*node.name, true); }
            void handle(const syntax::IdentifierNameSyntax& node) { name(node); }
            void handle(const syntax::IdentifierSelectNameSyntax& node) {
                const Symbol* s = scope.lookupName(node.identifier.valueText());
                result = combine(result, s ? classifier.symbol(*s) : Dependency::Undecided);
                for (auto* selector : node.selectors) selector->visit(*this);
            }
            // The result of a call need not depend on its arguments.
            void handle(const syntax::InvocationExpressionSyntax& node) {
                if (node.left->kind == syntax::SyntaxKind::SystemName && node.arguments &&
                    node.left->as<syntax::SystemNameSyntax>().systemIdentifier.valueText() == "$bits" &&
                    node.arguments->parameters.size() == 1) {
                    auto* arg = node.arguments->parameters[0];
                    syntax::SyntaxPrinter printer;
                    printer.setIncludeTrivia(false).setExpandMacros(true).print(*arg);
                    auto name = printer.str();
                    // Only a single plain or package-qualified name is resolved here.
                    if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$:") == std::string::npos) {
                        auto* hit = scope.lookupName(name);
                        if (!hit) hit = scope.lookupName(name, LookupLocation::max, LookupFlags::Type);
                        if (hit) {
                            if (hit->isValue() && hit->kind != SymbolKind::Parameter && hit->kind != SymbolKind::EnumValue) {
                                auto* dt = hit->getDeclaredType();
                                result = combine(result, dt ? classifier.declared(*dt, hit->getParentScope()) : Dependency::Undecided);
                            }
                            else result = combine(result, classifier.symbol(*hit));
                            return;
                        }
                    }
                }
                // `$clog2(N)` is a constant function of its argument: it depends on exactly what the argument depends on, so a width written
                // with it is parameter-dependent when N is, and fixed when N is.
                if (node.left->kind == syntax::SyntaxKind::SystemName && node.arguments &&
                    node.left->as<syntax::SystemNameSyntax>().systemIdentifier.valueText() == "$clog2") {
                    for (auto* argument : node.arguments->parameters) argument->visit(*this);
                    return;
                }
                // A user function of parameters, written `f(P + 1)` or `pkg::f(P + 1)`.
                if (node.arguments && (node.left->kind == syntax::SyntaxKind::IdentifierName || node.left->kind == syntax::SyntaxKind::ScopedName)) {
                    syntax::SyntaxPrinter printer;
                    printer.setIncludeTrivia(false).setExpandMacros(true).print(*node.left);
                    if (const Symbol* callee = scope.lookupName(printer.str()); callee && callee->kind == SymbolKind::Subroutine) {
                        Dependency d = classifier.function(callee->as<SubroutineSymbol>());
                        for (auto* argument : node.arguments->parameters) {
                            Scan arg(classifier, scope);
                            argument->visit(arg);
                            d = combine(d, arg.result);
                        }
                        result = combine(result, d);
                        return;
                    }
                }
                result = combine(result, Dependency::Undecided);
            }
            void handle(const syntax::ScopedNameSyntax& node) { name(node); }
            void handle(const syntax::SystemNameSyntax&) { result = combine(result, Dependency::Undecided); }
        };

        Dependency syntax(const syntax::SyntaxNode* node, const Scope* scope) {
            if (!node || !scope || depth >= 32) return Dependency::Undecided;
            Scan scan(*this, *scope);
            node->visit(scan);
            return scan.result;
        }
    };

    // Provenance of the width of an expression, distinct from provenance of its value.
    struct WidthProvenance {
        Classifier c;
        const Scope* scope;
        explicit WidthProvenance(const Scope* scope) : scope(scope) {}
        Dependency object(const Symbol& s) {
            auto* dt = s.getDeclaredType();
            if (dt && dt->getTypeSyntax()) return c.declared(*dt, s.getParentScope());
            // Packed fields are assigned their resolved type by slang; their declarator
            // retains the enclosing member declaration with the original type syntax.
            const auto* decl = s.getSyntax();
            if (s.kind == SymbolKind::Field && decl && decl->parent &&
                decl->parent->kind == syntax::SyntaxKind::StructUnionMember) {
                auto& member = decl->parent->as<syntax::StructUnionMemberSyntax>();
                Dependency d = c.syntax(member.type, s.getParentScope());
                const auto& field = decl->as<syntax::DeclaratorSyntax>();
                for (auto* dimension : field.dimensions)
                    d = combine(d, c.syntax(dimension, s.getParentScope()));
                return d;
            }
            return Dependency::Undecided;
        }
        Dependency value(const Expression& expr) {
            const auto& e = unwrap(expr);
            switch (e.kind) {
                case ExpressionKind::IntegerLiteral:
                case ExpressionKind::UnbasedUnsizedIntegerLiteral: return Dependency::Fixed;
                case ExpressionKind::NamedValue: return c.symbol(e.as<NamedValueExpression>().symbol);
                case ExpressionKind::HierarchicalValue: return c.symbol(e.as<HierarchicalValueExpression>().symbol);
                case ExpressionKind::UnaryOp: return value(e.as<UnaryExpression>().operand());
                case ExpressionKind::BinaryOp: {
                    auto& b = e.as<BinaryExpression>();
                    return combine(value(b.left()), value(b.right()));
                }
                case ExpressionKind::ElementSelect: {
                    auto& x = e.as<ElementSelectExpression>();
                    return combine(value(x.value()), value(x.selector()));
                }
                case ExpressionKind::RangeSelect: {
                    auto& x = e.as<RangeSelectExpression>();
                    return combine(value(x.value()), combine(value(x.left()), value(x.right())));
                }
                default: return Dependency::Undecided;
            }
        }
        Dependency width(const Expression& expr) {
            const auto& e = unwrap(expr);
            switch (e.kind) {
                case ExpressionKind::IntegerLiteral:
                case ExpressionKind::UnbasedUnsizedIntegerLiteral: return Dependency::Fixed;
                case ExpressionKind::NamedValue: {
                    auto& s = e.as<NamedValueExpression>().symbol;
                    return s.kind == SymbolKind::Parameter || s.kind == SymbolKind::EnumValue ? c.symbol(s) : object(s);
                }
                case ExpressionKind::HierarchicalValue: {
                    auto& s = e.as<HierarchicalValueExpression>().symbol;
                    return s.kind == SymbolKind::Parameter || s.kind == SymbolKind::EnumValue ? c.symbol(s) : object(s);
                }
                case ExpressionKind::MemberAccess: return object(e.as<MemberAccessExpression>().member);
                case ExpressionKind::Assignment: {
                    const auto& assignment = e.as<AssignmentExpression>();
                    // Output arguments are synthetic assignments to the connected
                    // destination, whose width is the actual connection width.
                    return assignment.isLValueArg() ? width(assignment.left()) : Dependency::Undecided;
                }
                case ExpressionKind::ElementSelect: {
                    auto& x = e.as<ElementSelectExpression>();
                    if (!x.value().type) return Dependency::Undecided;
                    if (x.value().type->isSimpleBitVector()) return Dependency::Fixed;
                    const Expression& base = unwrap(x.value());
                    const Symbol* sym = base.kind == ExpressionKind::NamedValue ? &base.as<NamedValueExpression>().symbol :
                                        base.kind == ExpressionKind::HierarchicalValue ? &base.as<HierarchicalValueExpression>().symbol : nullptr;
                    if (!sym) return Dependency::Undecided;
                    auto* dt = sym->getDeclaredType();
                    if (!dt || !dt->getTypeSyntax()) return Dependency::Undecided;
                    if (x.value().type->isUnpackedArray())
                        return e.type && e.type->isIntegral() ? object(*sym) : Dependency::Undecided;
                    auto* ts = dt->getTypeSyntax();
                    if (!syntax::IntegerTypeSyntax::isKind(ts->kind)) return Dependency::Undecided;
                    auto& dims = ts->as<syntax::IntegerTypeSyntax>().dimensions;
                    if (dims.empty()) return Dependency::Undecided;
                    Dependency d = Dependency::Fixed;
                    for (size_t i = 1; i < dims.size(); ++i)
                        d = combine(d, c.syntax(dims[i], sym->getParentScope()));
                    return d;
                }
                case ExpressionKind::RangeSelect: {
                    auto& x = e.as<RangeSelectExpression>();
                    auto bounds = value(x.right());
                    if (x.getSelectionKind() == RangeSelectionKind::Simple) bounds = combine(bounds, value(x.left()));
                    if (!x.value().type || !x.value().type->isSimpleBitVector()) bounds = combine(bounds, Dependency::Undecided);
                    return bounds;
                }
                case ExpressionKind::Concatenation: {
                    Dependency d = Dependency::Fixed;
                    for (auto* op : e.as<ConcatenationExpression>().operands()) d = combine(d, width(*op));
                    return d;
                }
                case ExpressionKind::Replication: {
                    auto& x = e.as<ReplicationExpression>();
                    return combine(value(x.count()), width(x.concat()));
                }
                case ExpressionKind::BinaryOp: {
                    auto& b = e.as<BinaryExpression>();
                    if (isLeftShift(b.op) || b.op == BinaryOperator::LogicalShiftRight || b.op == BinaryOperator::ArithmeticShiftRight)
                        return width(b.left());
                    if (b.op == BinaryOperator::Add || b.op == BinaryOperator::Subtract ||
                        b.op == BinaryOperator::Multiply || b.op == BinaryOperator::Divide ||
                        b.op == BinaryOperator::Mod || b.op == BinaryOperator::BinaryAnd ||
                        b.op == BinaryOperator::BinaryOr || b.op == BinaryOperator::BinaryXor)
                        return combine(width(b.left()), width(b.right()));
                    return Dependency::Undecided;
                }
                case ExpressionKind::UnaryOp: return Dependency::Undecided;
                case ExpressionKind::ConditionalOp: {
                    auto& x = e.as<ConditionalExpression>();
                    return combine(width(x.left()), width(x.right()));
                }
                case ExpressionKind::Conversion: {
                    auto& x = e.as<ConversionExpression>();
                    if (x.isImplicit()) return width(x.operand());
                    if (!e.syntax || e.syntax->kind != syntax::SyntaxKind::CastExpression) return Dependency::Undecided;
                    return c.syntax(e.syntax->as<syntax::CastExpressionSyntax>().left, scope);
                }
                default: return Dependency::Undecided;
            }
        }
    };

    static std::string paramValues(const InstanceBodySymbol* body) {
        if (!body) return "parameters of the enclosing package or unit";
        std::string s;
        for (auto& p : body->membersOfType<ParameterSymbol>()) {
            if (p.isLocalParam()) continue;
            s += (s.empty() ? "" : ", ") + std::string(p.name) + "=" + p.getValue().toString();
        }
        return s.empty() ? "type or enclosing parameters" : s;
    }

    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        const Scope* scope = nullptr;
        template<typename T> void visit(const T& node) {
            if constexpr (std::is_base_of_v<Scope, T>) {
                auto* saved = scope;
                scope = &node;
                HardwareVisitor<V, VisitFlags::AllCanonical>::visit(node);
                scope = saved;
            }
            else HardwareVisitor<V, VisitFlags::AllCanonical>::visit(node);
        }
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        ValueRanges ranges;
        explicit V(Lint& lint) : l(lint), ranges(lint) {}

        // A branch condition that compares a variable with a constant bounds it inside the branch (as for LNT-036), unless the procedure
        // writes it by a blocking assignment: `if (cnt_q < N) cnt_d = cnt_q + 1;` then stores a value the target holds.
        lint::BlockingWrites writes;
        void handle(const ProceduralBlockSymbol& p) {
            auto outer = std::move(writes);
            writes = lint::blockingWritesIn(p.getBody());
            visitDefault(p);
            writes = std::move(outer);
        }
        void handle(const SubroutineSymbol& r) {
            auto outer = std::move(writes);
            writes = lint::blockingWritesIn(r.getBody());
            visitDefault(r);
            writes = std::move(outer);
        }
        std::vector<ValueRanges::Guard> guards(const Expression& cond, bool truth) const {
            if (writes.unknown) return {};
            auto found = ranges.guardsOf(cond, truth);
            std::erase_if(found, [&](const ValueRanges::Guard& g) { return writes.symbols.count(g.var) != 0; });
            return found;
        }
        void handle(const ConditionalStatement& s) {
            if (s.conditions.size() != 1 || s.conditions[0].pattern) { visitDefault(s); return; }
            const Expression& cond = *s.conditions[0].expr;
            cond.visit(*this);
            { ValueRanges::Narrowing n(ranges, guards(cond, true)); s.ifTrue.visit(*this); }
            if (s.ifFalse) { ValueRanges::Narrowing n(ranges, guards(cond, false)); s.ifFalse->visit(*this); }
        }
        void handle(const ConditionalExpression& c) {
            if (c.conditions.size() != 1 || c.conditions[0].pattern) { visitDefault(c); return; }
            const Expression& cond = *c.conditions[0].expr;
            cond.visit(*this);
            { ValueRanges::Narrowing n(ranges, guards(cond, true)); c.left().visit(*this); }
            { ValueRanges::Narrowing n(ranges, guards(cond, false)); c.right().visit(*this); }
        }

        // Reports a truncation of `from` bits into `to` bits when either side's declared width depends on a parameter.
        void truncation(slang::SourceLocation at, Dependency p1, Dependency p2, bitwidth_t from, bitwidth_t to,
                        const InstanceBodySymbol* body, const std::string& what) {
            l.count();
            Dependency dep = combine(p1, p2);
            if (dep == Dependency::Undecided) {
                reportIn(l, path, at, what + " truncates " + std::to_string(from) + " to " + std::to_string(to) +
                                 " bits; whether the widths depend on a parameter could not be determined",
                         {.confidence = Confidence::Unresolved, .nextStep = "Declare the widths in the source text so the parameter dependence can be checked.",
                          .hierarchy = body ? body->getHierarchicalPath() : ""});
                return;
            }
            if (dep == Dependency::Fixed) return;
            reportIn(l, path, at, what + " truncates " + std::to_string(from) + " to " + std::to_string(to) +
                             " bits and a width has parameter provenance (" + paramValues(body) + ")",
                     {.confidence = Confidence::Heuristic,
                      .nextStep = "Check the truncation for every parameter value this module can take, or make it explicit with a cast.",
                      .hierarchy = body ? body->getHierarchicalPath() : ""});
        }

        void unknownAggregate(slang::SourceLocation at, const InstanceBodySymbol* body, const std::string& what) {
            l.count();
            reportIn(l, path, at, what + " may truncate an aggregate element; width provenance could not be determined",
                     {.confidence = Confidence::Unresolved,
                      .nextStep = "Review the element conversions and parameter configurations.",
                      .hierarchy = body ? body->getHierarchicalPath() : ""});
        }

        void patternConversions(const Expression& pattern, const InstanceBodySymbol* body, const std::string& what) {
            struct Scan : ASTVisitor<Scan, VisitFlags::AllGood> {
                Lint& lint;
                bool loss = false;
                explicit Scan(Lint& l) : lint(l) {}
                void handle(const ConversionExpression& conversion) {
                    const Expression& source = unwrap(conversion.operand());
                    const auto target = widthOf(conversion), from = neededWidth(lint, source);
                    if (conversion.isImplicit() && target && from && *from > *target) {
                        const auto value = lint.evalConst(source);
                        bool preserved = false;
                        if (value.isInteger() && !value.integer().hasUnknown()) {
                            const auto& v = value.integer();
                            auto stored = v.resize(*target);
                            const bool sign = conversion.type->isSigned();
                            stored.setSigned(sign);
                            const auto back = stored.extend(std::max(*target, v.getBitWidth()), sign);
                            preserved = bool(back == v) && !(v.isSigned() && v.isNegative() && !sign);
                        }
                        loss = loss || !preserved;
                    }
                    visitDefault(conversion);
                }
            } scan(l);
            pattern.visit(scan);
            if (scan.loss) unknownAggregate(pattern.sourceRange.start(), body, what);
        }

        // Two unpacked arrays of the same shape whose elements have the same widths convert element by element without a change of width.
        static bool sameElementWidths(const Type& a, const Type& b) {
            const Type& x = a.getCanonicalType();
            const Type& y = b.getCanonicalType();
            if (x.isUnpackedArray() && y.isUnpackedArray()) {
                if (!x.hasFixedRange() || !y.hasFixedRange() || x.getFixedRange().width() != y.getFixedRange().width()) return false;
                const Type* ex = x.getArrayElementType();
                const Type* ey = y.getArrayElementType();
                return ex && ey && sameElementWidths(*ex, *ey);
            }
            return x.isIntegral() && y.isIntegral() && x.getBitWidth() == y.getBitWidth();
        }

        void aggregate(slang::SourceLocation at, const Type& target, const Expression& source, Dependency targetDep,
                       const InstanceBodySymbol* body, const std::string& what, const Scope* sourceScope) {
            const Expression& r = unwrap(source);
            const bool pattern = r.kind == ExpressionKind::SimpleAssignmentPattern || r.kind == ExpressionKind::StructuredAssignmentPattern ||
                                 r.kind == ExpressionKind::ReplicatedAssignmentPattern;
            if (!pattern) {
                if (!r.type || (!target.isMatching(*r.type) && !sameElementWidths(target, *r.type))) unknownAggregate(at, body, what);
                return;
            }
            auto* elementType = target.getArrayElementType();
            if (!elementType || !elementType->isIntegral()) { unknownAggregate(at, body, what); return; }
            auto tw = elementType->getBitWidth();
            WidthProvenance provenance(sourceScope);
            // Every element a pattern gives, however it is written (a list, `default:` or a replication): a fill literal ('0, '1)
            // takes the element's width; a constant is checked against it; anything else has its width compared.
            const std::span<const Expression* const> elements =
                r.kind == ExpressionKind::SimpleAssignmentPattern       ? r.as<SimpleAssignmentPatternExpression>().elements()
                : r.kind == ExpressionKind::StructuredAssignmentPattern ? r.as<StructuredAssignmentPatternExpression>().elements()
                                                                        : r.as<ReplicatedAssignmentPatternExpression>().elements();
            for (auto* element : elements) {
                const Expression& e = unwrap(*element);
                if (e.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral) continue;
                if (e.kind == ExpressionKind::SimpleAssignmentPattern || e.kind == ExpressionKind::StructuredAssignmentPattern ||
                    e.kind == ExpressionKind::ReplicatedAssignmentPattern) {
                    patternConversions(e, body, what);
                    continue;
                }
                auto ew = neededWidth(l, e);
                if (!ew) { unknownAggregate(e.sourceRange.start(), body, what); continue; }
                if (*ew <= tw) continue;
                auto cv = l.evalConst(e);
                bool preserved = false;
                if (cv.isInteger() && !cv.integer().hasUnknown()) {
                    const auto& v = cv.integer();
                    auto stored = v.resize(tw);
                    bool targetSigned = elementType->isSigned();
                    stored.setSigned(targetSigned);
                    auto back = stored.extend(std::max(tw, v.getBitWidth()), targetSigned);
                    preserved = bool(back == v) && !(v.isSigned() && v.isNegative() && !targetSigned);
                }
                if (!preserved)
                    truncation(e.sourceRange.start(), targetDep, provenance.width(e), *ew, tw, body, what + " element");
            }
        }

        const InstanceBodySymbol* currentBody() const {
            return scope && scope->asSymbol().kind == SymbolKind::InstanceBody ? &scope->asSymbol().as<InstanceBodySymbol>()
                   : scope ? enclosingBody(scope->asSymbol()) : nullptr;
        }

        // One assignment of `source` to a target of type `type`: `leftDep` is the target's width provenance (lazy,
        // since it is only needed for a truncation); `what` names the construct in the message.
        template <typename LeftDep>
        void assigned(slang::SourceLocation at, const Type& type, const Expression& source, LeftDep leftDep, const char* what) {
            const Expression& r = unwrap(source);
            const auto* body = currentBody();
            if (type.isUnpackedArray()) {
                WidthProvenance provenance(scope);
                aggregate(at, type, r, leftDep(provenance), body, what, scope);
            }
            else if (r.kind == ExpressionKind::SimpleAssignmentPattern || r.kind == ExpressionKind::StructuredAssignmentPattern ||
                     r.kind == ExpressionKind::ReplicatedAssignmentPattern)
                patternConversions(r, body, what);
            OptWidth lw = type.isIntegral() ? OptWidth(type.getBitWidth()) : std::nullopt;
            auto rw = neededWidth(l, r);
            // A signed literal's sign bit is not necessarily a meaningful value bit
            // in an unsigned target. Prove preservation under the destination type.
            auto value = l.evalConst(r);
            bool preserved = false;
            if (lw && value.isInteger() && !value.integer().hasUnknown()) {
                const auto& v = value.integer();
                auto stored = v.resize(*lw);
                bool targetSigned = type.isSigned();
                stored.setSigned(targetSigned);
                auto back = stored.extend(std::max(*lw, v.getBitWidth()), targetSigned);
                preserved = bool(back == v) &&
                            !(v.isSigned() && v.isNegative() && !targetSigned);
            }
            // The value range under the branch guards fits the target: `if (cnt_q < N) cnt_d = cnt_q + 1;` loses nothing.
            if (!preserved && lw && *lw > 0 && *lw <= 62)
                if (const auto range = ranges.of(r)) {
                    const Wide top = Wide(1) << (type.isSigned() ? *lw - 1 : *lw);
                    preserved = range->within(type.isSigned() ? Interval{-top, top - 1} : Interval{0, top - 1});
                }
            if (lw && rw && *rw > *lw && !preserved) {
                WidthProvenance provenance(scope);
                truncation(at, leftDep(provenance), provenance.width(r), *rw, *lw, body, what);
            }
        }

        void handle(const AssignmentExpression& a) {
            if (!a.isCompound() && !a.isLValueArg() && l.isUser(a.sourceRange.start()) && a.left().type)
                assigned(a.sourceRange.start(), *a.left().type, a.right(),
                         [&](WidthProvenance& p) { return p.width(a.left()); }, "assignment");
            visitDefault(a);
        }

        // A declaration initializer is an assignment to the declared object.
        template <typename T> void initializer(const T& v) {
            if (const auto* init = v.getInitializer(); init && !init->bad() && l.isUser(init->sourceRange.start()))
                assigned(init->sourceRange.start(), v.getType(), *init,
                         [&](WidthProvenance& p) { return p.object(v); }, "initializer");
        }
        void handle(const VariableSymbol& v) {
            if (v.kind == SymbolKind::Variable) initializer(v);
            visitDefault(v);
        }
        void handle(const NetSymbol& n) {
            initializer(n);
            visitDefault(n);
        }

        void handle(const InstanceSymbol& inst) {
            for (auto* pc : inst.getPortConnections()) {
                const Expression* ex = pc->getExpression();
                if (!ex || pc->port.kind != SymbolKind::Port) continue;
                auto& port = pc->port.as<PortSymbol>();
                auto pw = port.getType().isIntegral() ? OptWidth(port.getType().getBitWidth()) : std::nullopt;
                const Expression& actual = unwrap(*ex);
                if (!l.isUser(actual.sourceRange.start())) continue;
                if (port.direction == ArgumentDirection::In && port.getType().isUnpackedArray()) {
                    WidthProvenance provenance(inst.getParentScope());
                    const Symbol& formal = port.internalSymbol ? *port.internalSymbol : port;
                    aggregate(actual.sourceRange.start(), port.getType(), actual, provenance.object(formal), &inst.body,
                              "input port '" + std::string(port.name) + "' of '" + std::string(inst.name) + "'", inst.getParentScope());
                }
                else if (port.direction == ArgumentDirection::Out && port.getType().isUnpackedArray() &&
                         actual.type && !port.getType().isMatching(*actual.type))
                    unknownAggregate(actual.sourceRange.start(), &inst.body, "output port '" + std::string(port.name) + "'");
                if (port.direction == ArgumentDirection::In && !port.getType().isUnpackedArray() &&
                    (actual.kind == ExpressionKind::SimpleAssignmentPattern || actual.kind == ExpressionKind::StructuredAssignmentPattern ||
                     actual.kind == ExpressionKind::ReplicatedAssignmentPattern))
                    patternConversions(actual, &inst.body, "input port '" + std::string(port.name) + "'");
                auto aw = widthOf(actual);
                if (!pw || !aw || port.direction == ArgumentDirection::InOut || port.direction == ArgumentDirection::Ref)
                    continue;
                bool input = port.direction == ArgumentDirection::In;
                const uint32_t pwv = *pw, awv = *aw;
                if (input ? awv <= pwv : pwv <= awv) continue;
                WidthProvenance provenance(inst.getParentScope());
                const Symbol& formal = port.internalSymbol ? *port.internalSymbol : port;
                std::string what = std::string(input ? "input" : "output") + " port '" + std::string(port.name) + "' of '" +
                                   std::string(inst.name) + "'";
                truncation(actual.sourceRange.start(), provenance.object(formal), provenance.width(actual), input ? awv : pwv, input ? pwv : awv, &inst.body, what);
            }
            visitDefault(inst);
        }
    };
    void check(Lint& l) override { visitDesign<V>(l); }
};

} // namespace

// The values a non-constant operand can take: those of a `for` loop variable bounded by constants, otherwise its type's.
// Used where slang decides only constant operands: an index (LNT-006) and a divisor (LNT-037).
// A loop's range bounds the values its variable can take; it does not say each occurs, so a range outside the array or
// including zero stays Unresolved.

std::string operandText(const Expression& e, Lint& l) {
    const auto& operand = unwrap(e);
    if (operand.kind == ExpressionKind::NamedValue) return std::string(operand.as<NamedValueExpression>().symbol.name);
    return std::string(l.sm().getSourceText(operand.sourceRange.start().buffer()).substr(
        operand.sourceRange.start().offset(), operand.sourceRange.end().offset() - operand.sourceRange.start().offset()));
}

// (some condition is constant false, every condition is constant true) for the conditions of an if or a `?:`.
template<typename Conds> std::pair<bool, bool> constantConditions(Lint& l, const Conds& conditions) {
    bool anyFalse = false, allTrue = true;
    for (const auto& c : conditions) {
        auto v = c.pattern ? slang::ConstantValue() : l.evalConst(*c.expr);
        anyFalse = anyFalse || (v && v.isFalse());
        allTrue = allTrue && v && v.isTrue();
    }
    return {anyFalse, allTrue};
}

// LNT-006, non-constant part: an index whose values reach outside the array. Constant indexes are slang's (LNT-006 above).
class IndexRangeRule final : public LintRule {
public:
    IndexRangeRule() : LintRule("LNT-006", "", true) {}

private:
    struct V : ConditionalDepthVisitor<V> {
        using ConditionalDepthVisitor<V>::handle;
        Lint& l;
        OperandRanges ranges;
        ValueRanges vr;   // the range of an index expression; `ranges` serves the always-on proof
        bool optIn = false;   // dynamic_index=true: also an index fixed by constants and loop counters that may overrun
        bool off = false;     // dynamic_index=false: no (b) at all; by default a free input's range is checked
        // (c): loop variables whose loop runs every iteration to its bound, with the conditional depth of their body.
        // A select at exactly that depth runs on every iteration; anything under a condition, a short-circuit or an
        // inner loop is guarded and left to the opt-in (b).
        std::vector<std::pair<const ValueSymbol*, int>> proven;
        explicit V(Lint& lint, const std::set<const SubroutineSymbol*>& reached)
            : l(lint), vr(lint, true), called(reached), optIn(lint.option("dynamic_index", "inputs") == "true"), off(lint.option("dynamic_index", "inputs") == "false") {}
        // Code a constant condition excludes (`if (N > 8)` with N = 4) is not part of this design: no select there runs.
        int dead = 0;
        // A user task or function nobody calls in this design: its overruns are real if the code is reused, so they stay
        // findings, Unresolved instead of Established.
        const std::set<const SubroutineSymbol*>& called;
        bool inUncalled = false;
        std::set<slang::SourceLocation> provenAt;   // selects reported as errors
        std::vector<std::pair<slang::SourceLocation, std::function<void()>>> uncalled;   // deferred "never called" findings
        void flush() { for (auto& [at, report] : uncalled) if (!provenAt.count(at)) report(); }
        // A `disable` of a block that holds both it and a later select skips that select when it runs: the select is not executed on
        // every iteration. The disable's own condition does not matter (a run-time one is the usual case, a constant one is no better).
        struct Skip { slang::SourceRange block; slang::SourceLocation at; };
        std::vector<Skip> skips;
        struct SkipFinder : ASTVisitor<SkipFinder, VisitFlags::AllGood> {
            std::map<const Symbol*, slang::SourceRange> blocks;
            std::vector<std::pair<const Symbol*, slang::SourceLocation>> disables;
            void handle(const BlockStatement& b) {
                if (b.blockSymbol) blocks[b.blockSymbol] = b.sourceRange;
                visitDefault(b);
            }
            void handle(const DisableStatement& d) {
                d.target.visitSymbolReferences([&](const Expression&, const Symbol& sym) { disables.push_back({&sym, d.sourceRange.start()}); });
                visitDefault(d);
            }
        };
        void collectSkips(const Statement& body) {
            SkipFinder f;
            body.visit(f);
            for (auto& [sym, at] : f.disables)
                if (auto it = f.blocks.find(sym); it != f.blocks.end() && it->second.start() <= at && at <= it->second.end())
                    skips.push_back({it->second, at});
        }
        bool skippedBefore(slang::SourceLocation select) const {
            return std::any_of(skips.begin(), skips.end(), [&](const Skip& k) {
                if (k.block.start().buffer() != select.buffer() || k.at.buffer() != select.buffer()) return true;   // a macro: not placed
                return k.block.start() <= select && select <= k.block.end() && k.at < select;
            });
        }
        // An index that is an exact affine function s*v + k of one loop variable with a known range (+, -, * and << by constants,
        // unary minus, conversions that keep the value), every node evaluated without wrapping in its own type: both ends of its
        // range are values the loop takes, so an end outside the array is an overrun on an iteration that runs.
        struct Affine { const ValueSymbol* var = nullptr; Wide s = 0, k = 0; };
        std::optional<Interval> affineRange(const Affine& a) const {
            if (!a.var) return Interval{a.k, a.k};
            const auto r = vr.loopRange(a.var);
            if (!r) return std::nullopt;
            const Wide x = a.s * r->lo + a.k, y = a.s * r->hi + a.k;
            return Interval{std::min(x, y), std::max(x, y)};
        }
        std::optional<Affine> affine(const Expression& e) const {
            if (!e.type || !e.type->isIntegral()) return std::nullopt;
            std::optional<Affine> a;
            if (const auto c = lint::constantInt(l, e)) a = Affine{nullptr, 0, *c};
            else if (e.kind == ExpressionKind::NamedValue) {
                const auto* sym = &e.as<NamedValueExpression>().symbol;
                if (vr.isLoopVar(sym)) a = Affine{sym, 1, 0};
            }
            else if (e.kind == ExpressionKind::Conversion) a = affine(e.as<ConversionExpression>().operand());
            else if (e.kind == ExpressionKind::UnaryOp) {
                const auto& u = e.as<UnaryExpression>();
                a = affine(u.operand());
                if (a && u.op == UnaryOperator::Minus) a = Affine{a->var, -a->s, -a->k};
                else if (!(a && u.op == UnaryOperator::Plus)) a.reset();
            }
            else if (e.kind == ExpressionKind::BinaryOp) {
                const auto& b = e.as<BinaryExpression>();
                const auto x = affine(b.left());
                if (b.op == BinaryOperator::LogicalShiftLeft || b.op == BinaryOperator::ArithmeticShiftLeft) {
                    const auto count = lint::constantInt(l, b.right());
                    if (x && count && *count >= 0 && *count <= 40) a = Affine{x->var, x->s * (Wide(1) << *count), x->k * (Wide(1) << *count)};
                }
                else if (const auto y = affine(b.right()); x && y && (!x->var || !y->var || x->var == y->var)) {
                    const auto* var = x->var ? x->var : y->var;
                    if (b.op == BinaryOperator::Add) a = Affine{var, x->s + y->s, x->k + y->k};
                    else if (b.op == BinaryOperator::Subtract) a = Affine{var, x->s - y->s, x->k - y->k};
                    else if (b.op == BinaryOperator::Multiply && (x->s == 0 || y->s == 0))
                        a = x->s == 0 ? Affine{y->var, x->k * y->s, x->k * y->k} : Affine{x->var, y->k * x->s, y->k * x->k};
                }
            }
            if (!a) return std::nullopt;
            if (a->s == 0) a->var = nullptr;
            const auto r = affineRange(*a), full = typeRange(*e.type);
            if (!r || !full || !r->within(*full)) return std::nullopt;
            return a;
        }
        // What a condition says of one loop variable when it evaluates to `truth`: a sound over-approximation of its values.
        struct Constraint { const ValueSymbol* var; Interval range; };
        static Wide floorDiv(Wide a, Wide b) { Wide q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
        static Wide ceilDiv(Wide a, Wide b) { Wide q = a / b; return (a % b != 0 && ((a < 0) == (b < 0))) ? q + 1 : q; }
        std::optional<Constraint> constrain(const Expression& cond, bool truth) const {
            const Expression& e = unwrapImplicit(cond);
            if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot)
                return constrain(e.as<UnaryExpression>().operand(), !truth);
            if (e.kind != ExpressionKind::BinaryOp) return std::nullopt;
            const auto& b = e.as<BinaryExpression>();
            if (b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) {
                const bool both = (b.op == BinaryOperator::LogicalAnd) == truth;
                const auto x = constrain(b.left(), truth), y = constrain(b.right(), truth);
                if (both) {
                    if (x && y && x->var == y->var) return Constraint{x->var, {std::max(x->range.lo, y->range.lo), std::min(x->range.hi, y->range.hi)}};
                    return x ? x : y;
                }
                if (x && y && x->var == y->var) return Constraint{x->var, {std::min(x->range.lo, y->range.lo), std::max(x->range.hi, y->range.hi)}};
                return std::nullopt;
            }
            enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
            Cmp op;
            switch (b.op) {
                case BinaryOperator::LessThan: op = Cmp::Lt; break;
                case BinaryOperator::LessThanEqual: op = Cmp::Le; break;
                case BinaryOperator::GreaterThan: op = Cmp::Gt; break;
                case BinaryOperator::GreaterThanEqual: op = Cmp::Ge; break;
                case BinaryOperator::Equality: op = Cmp::Eq; break;
                case BinaryOperator::Inequality: op = Cmp::Ne; break;
                default: return std::nullopt;
            }
            auto side = affine(b.left());
            auto other = lint::constantInt(l, b.right());
            if (!side || !side->var || !other) {
                side = affine(b.right());
                other = lint::constantInt(l, b.left());
                if (!side || !side->var || !other) return std::nullopt;
                if (op == Cmp::Lt) op = Cmp::Gt; else if (op == Cmp::Gt) op = Cmp::Lt;
                else if (op == Cmp::Le) op = Cmp::Ge; else if (op == Cmp::Ge) op = Cmp::Le;
            }
            if (!truth) {
                switch (op) {
                    case Cmp::Lt: op = Cmp::Ge; break;
                    case Cmp::Le: op = Cmp::Gt; break;
                    case Cmp::Gt: op = Cmp::Le; break;
                    case Cmp::Ge: op = Cmp::Lt; break;
                    case Cmp::Eq: op = Cmp::Ne; break;
                    case Cmp::Ne: op = Cmp::Eq; break;
                }
            }
            const Wide inf = Wide(1) << 100, t = Wide(*other) - side->k, s = side->s;
            Interval v{-inf, inf};   // s*var <= m, s*var >= m, s*var == t
            const auto atMost = [&](Wide m) { if (s > 0) v.hi = floorDiv(m, s); else v.lo = ceilDiv(m, s); };
            const auto atLeast = [&](Wide m) { if (s > 0) v.lo = ceilDiv(m, s); else v.hi = floorDiv(m, s); };
            switch (op) {
                case Cmp::Lt: atMost(t - 1); break;
                case Cmp::Le: atMost(t); break;
                case Cmp::Gt: atLeast(t + 1); break;
                case Cmp::Ge: atLeast(t); break;
                case Cmp::Eq:
                    if (t % s == 0) v = {t / s, t / s};
                    else v = {1, 0};   // no value of the variable satisfies it
                    break;
                case Cmp::Ne: {
                    // Only an end of the range can be cut off.
                    const auto range = vr.loopRange(side->var);
                    if (!range || t % s != 0) return std::nullopt;
                    const Wide p = t / s;
                    if (p == range->lo) v.lo = p + 1;
                    else if (p == range->hi) v.hi = p - 1;
                    else return std::nullopt;
                    break;
                }
            }
            return Constraint{side->var, v};
        }
        // Inside a branch a condition decides, the loop variable takes only the values the condition allows.
        struct Narrowed {
            V& v;
            const ValueSymbol* var = nullptr;
            std::optional<Interval> saved;
            bool never = false;
            Narrowed(V& visitor, const std::optional<Constraint>& c) : v(visitor) {
                if (!c) return;
                saved = v.vr.narrow(c->var, c->range, never);
                if (!saved) return;
                var = c->var;
                v.dead += never;
            }
            ~Narrowed() {
                if (!var) return;
                v.vr.restore(var, saved);
                v.dead -= never;
            }
        };
        // A branch condition that compares a variable (or a net assigned such a comparison) with a constant bounds it inside the branch,
        // unless the procedure writes it by a blocking assignment: then the value the condition tested is not the value the branch reads.
        lint::BlockingWrites writes;
        std::vector<ValueRanges::Guard> guards(const Expression& cond, bool truth) const {
            if (writes.unknown) return {};
            auto found = vr.guardsOf(cond, truth);
            std::erase_if(found, [&](const ValueRanges::Guard& g) { return writes.symbols.count(g.var) != 0; });
            return found;
        }
        void handle(const ProceduralBlockSymbol& p) {
            auto outer = std::move(skips);
            auto outerWrites = std::move(writes);
            skips.clear();
            writes = lint::blockingWritesIn(p.getBody());
            collectSkips(p.getBody());
            visitDefault(p);
            skips = std::move(outer);
            writes = std::move(outerWrites);
        }
        void handle(const SubroutineSymbol& r) {
            const bool outer = inUncalled;
            inUncalled = !called.count(&r);
            auto outerSkips = std::move(skips);
            auto outerWrites = std::move(writes);
            skips.clear();
            writes = lint::blockingWritesIn(r.getBody());
            collectSkips(r.getBody());
            enclosing.push_back(&r);
            visitDefault(r);
            enclosing.pop_back();
            skips = std::move(outerSkips);
            writes = std::move(outerWrites);
            inUncalled = outer;
        }
        template<typename Node> void branch(const Node& s, bool excluded) {
            dead += excluded;
            s.visit(*this);
            dead -= excluded;
        }
        void handle(const ConditionalStatement& s) {
            bool anyFalse = false, allTrue = true;
            for (const auto& c : s.conditions) {
                auto v = c.pattern ? slang::ConstantValue() : l.evalConst(*c.expr);
                anyFalse = anyFalse || (v && v.isFalse());
                allTrue = allTrue && v && v.isTrue();
            }
            if (!anyFalse && !allTrue) {
                if (s.conditions.size() != 1 || s.conditions[0].pattern) { ConditionalDepthVisitor<V>::handle(s); return; }
                // One run-time condition: a branch runs only for the values of a loop variable that the condition allows.
                const Expression& cond = *s.conditions[0].expr;
                cond.visit(*this);
                const auto whenTrue = constrain(cond, true), whenFalse = constrain(cond, false);
                ++guarded;
                { Narrowed n(*this, whenTrue); ValueRanges::Narrowing g(vr, guards(cond, true)); s.ifTrue.visit(*this); }
                if (s.ifFalse) { Narrowed n(*this, whenFalse); ValueRanges::Narrowing g(vr, guards(cond, false)); s.ifFalse->visit(*this); }
                --guarded;
                return;
            }
            for (const auto& c : s.conditions) {
                c.expr->visit(*this);
                if (c.pattern) c.pattern->visit(*this);
            }
            ++guarded;
            branch(s.ifTrue, anyFalse);
            if (s.ifFalse) branch(*s.ifFalse, allTrue);
            --guarded;
        }
        // The statements after `if (c) break;` (or `continue`, `return`, `disable`) in one list run only when `c` is false, so the
        // condition narrows what they read; the ones after an unconditional jump never run.
        // A `disable` leaves only a block (or the task) that encloses it: one that does not is not running, or is another process's, and
        // the statement goes on.
        std::vector<const Symbol*> enclosing;   // the named blocks and the subroutine around the statement being visited
        void handle(const BlockStatement& b) {
            if (b.blockSymbol) enclosing.push_back(b.blockSymbol);
            visitDefault(b);
            if (b.blockSymbol) enclosing.pop_back();
        }
        bool jumps(const Statement& s) {
            switch (s.kind) {
                case StatementKind::Break: case StatementKind::Continue: case StatementKind::Return: return true;
                case StatementKind::Disable: {
                    bool ours = false;
                    s.as<DisableStatement>().target.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                        ours = ours || std::find(enclosing.begin(), enclosing.end(), &sym) != enclosing.end();
                    });
                    return ours;
                }
                case StatementKind::Block: {
                    const auto& b = s.as<BlockStatement>();
                    if (b.blockKind != StatementBlockKind::Sequential) return false;
                    if (b.blockSymbol) enclosing.push_back(b.blockSymbol);
                    const bool result = jumps(b.body);
                    if (b.blockSymbol) enclosing.pop_back();
                    return result;
                }
                case StatementKind::List: {
                    const auto& list = s.as<StatementList>().list;
                    return !list.empty() && jumps(*list.back());
                }
                default: return false;
            }
        }
        void handle(const StatementList& list) {
            struct Held { std::unique_ptr<Narrowed> byLoop; std::unique_ptr<ValueRanges::Narrowing> byGuard; };
            std::vector<Held> held;
            int skipped = 0;
            for (const auto* st : list.list) {
                st->visit(*this);
                if (jumps(*st)) { ++dead; ++skipped; continue; }
                if (st->kind != StatementKind::Conditional) continue;
                const auto& c = st->as<ConditionalStatement>();
                if (c.ifFalse || c.conditions.size() != 1 || c.conditions[0].pattern || !jumps(c.ifTrue)) continue;
                if (l.evalConst(*c.conditions[0].expr)) continue;   // a constant condition is decided elsewhere
                const Expression& cond = *c.conditions[0].expr;
                held.push_back({std::make_unique<Narrowed>(*this, constrain(cond, false)),
                                std::make_unique<ValueRanges::Narrowing>(vr, guards(cond, false))});
            }
            while (!held.empty()) held.pop_back();   // restored in the reverse order they were set
            dead -= skipped;
        }
        void handle(const CaseStatement& s) {
            slang::ast::ASTContext ac(l.compilation().getRoot(), slang::ast::LookupLocation::max);
            slang::ast::EvalContext ec(ac);
            auto [taken, known] = s.getKnownBranch(ec);
            if (!known) { ConditionalDepthVisitor<V>::handle(s); return; }
            s.expr.visit(*this);
            ++guarded;
            for (const auto& item : s.items) {
                for (auto e : item.expressions) e->visit(*this);
                branch(*item.stmt, item.stmt.get() != taken);
            }
            if (s.defaultCase) branch(*s.defaultCase, s.defaultCase != taken);
            --guarded;
        }
        // The right operand of `&&` after a constant false (or of `||` after a constant true) never runs.
        void handle(const BinaryExpression& e) {
            const bool isAnd = e.op == BinaryOperator::LogicalAnd, isOr = e.op == BinaryOperator::LogicalOr;
            if (isAnd || isOr) {
                auto v = l.evalConst(e.left());
                if (v && (isAnd ? v.isFalse() : v.isTrue())) {
                    e.left().visit(*this);
                    ++guarded;
                    branch(e.right(), true);
                    --guarded;
                    return;
                }
                // The right operand runs only when the left is true (`&&`) or false (`||`).
                e.left().visit(*this);
                ++guarded;
                { Narrowed n(*this, constrain(e.left(), isAnd)); ValueRanges::Narrowing g(vr, guards(e.left(), isAnd)); e.right().visit(*this); }
                --guarded;
                return;
            }
            ConditionalDepthVisitor<V>::handle(e);
        }
        void handle(const ConditionalExpression& e) {
            bool anyFalse = false, allTrue = true;
            for (const auto& c : e.conditions) {
                auto v = c.pattern ? slang::ConstantValue() : l.evalConst(*c.expr);
                anyFalse = anyFalse || (v && v.isFalse());
                allTrue = allTrue && v && v.isTrue();
            }
            if (!anyFalse && !allTrue) {
                if (e.conditions.size() != 1 || e.conditions[0].pattern) { ConditionalDepthVisitor<V>::handle(e); return; }
                const Expression& cond = *e.conditions[0].expr;
                cond.visit(*this);
                const auto whenTrue = constrain(cond, true), whenFalse = constrain(cond, false);
                ++guarded;
                { Narrowed n(*this, whenTrue); ValueRanges::Narrowing g(vr, guards(cond, true)); e.left().visit(*this); }
                { Narrowed n(*this, whenFalse); ValueRanges::Narrowing g(vr, guards(cond, false)); e.right().visit(*this); }
                --guarded;
                return;
            }
            for (const auto& c : e.conditions) {
                c.expr->visit(*this);
                if (c.pattern) c.pattern->visit(*this);
            }
            ++guarded;
            dead += anyFalse; e.left().visit(*this); dead -= anyFalse;
            dead += allTrue; e.right().visit(*this); dead -= allTrue;
            --guarded;
        }
        void handle(const ForLoopStatement& s) {
            ranges.enter(s, l, true);
            const auto* var = ranges.boundedVar(s);
            const bool every = var && !lint::leavesEarly(s.body);
            ++guarded;
            if (every) proven.push_back({var, guarded});
            vr.visitLoop(*this, s);
            if (every) proven.pop_back();
            --guarded;
            ranges.leave(s);
        }
        // The selector is a loop variable, or an affine function of one, whose loop runs every iteration and whose select is at the body's depth.
        bool everyIteration(const Expression& selector, const std::optional<Affine>& aff) const {
            if (proven.empty()) return false;
            const Expression& operand = unwrap(selector);
            const ValueSymbol* var = nullptr;
            if (operand.kind == ExpressionKind::NamedValue) var = &operand.as<NamedValueExpression>().symbol;
            else if (aff && aff->var) var = aff->var;
            if (!var) return false;
            return std::any_of(proven.begin(), proven.end(), [&](const auto& p) { return p.first == var && p.second == guarded; });
        }
        void handle(const ElementSelectExpression& e) {
            visitDefault(e);
            const Type& array = e.value().type->getCanonicalType();
            if (dead || !l.isUser(e.sourceRange.start()) || !array.hasFixedRange() || OperandRanges::constant(l, e.selector())) return;
            const auto aff = affine(e.selector());
            const bool bare = unwrap(e.selector()).kind == ExpressionKind::NamedValue;
            bool proof = everyIteration(e.selector(), aff) && !skippedBefore(e.sourceRange.start());
            if (!proof && (off || (!optIn && vr.determined(e.selector())))) return;
            l.count();
            const auto bounds = array.getFixedRange();
            auto values = ranges.of(e.selector());
            if (proof && !bare)
                if (const auto r = affineRange(*aff)) values = ValueRange{int64_t(r->lo), int64_t(r->hi)};   // affine fits its type (affine())
            // A wrapping counter's range covers only its leading iterations: it proves an overrun, never that the index stays in range.
            if (values && ranges.wraps(e.selector()) && values->min >= bounds.lower() && values->max <= bounds.upper()) {
                if (!optIn) return;
                values.reset();
                proof = false;
            }
            if (values && values->min >= bounds.lower() && values->max <= bounds.upper()) return;
            const std::string index = operandText(e.selector(), l);
            const std::string range = "[" + std::to_string(bounds.lower()) + ":" + std::to_string(bounds.upper()) + "]";
            if (proof && values && inUncalled) {
                // One routine can be specialised in several instance bodies, called in one and not in another: the finding
                // for the uncalled body is dropped when the called one is an error at the same select (see flush).
                const auto at = e.selector().sourceRange.start();
                uncalled.push_back({at, [this, at, index, range, values = *values] {
                    l.report(at,
                             "loop index '" + index + "' runs from " + std::to_string(values.min) + " to " + std::to_string(values.max) +
                             " and leaves the selected range " + range + " on an iteration that always executes this select, but the task or function is never called in this design",
                             {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                              .nextStep = "Correct the loop bound or size the array to cover every iteration, or remove the unused routine.",
                              .signals = {index}});
                }});
                return;
            }
            if (proof && values) {
                provenAt.insert(e.selector().sourceRange.start());
                // A counter that steps by one takes the first value past the range; any other index, the end of its range.
                const bool firstPast = bare;
                const int64_t outside = values->max > bounds.upper() ? (firstPast ? std::max<int64_t>(values->min, bounds.upper() + 1) : values->max)
                                                                      : (firstPast ? std::min<int64_t>(values->max, bounds.lower() - 1) : values->min);
                l.report(e.selector().sourceRange.start(),
                         "loop index '" + index + "' runs from " + std::to_string(values->min) + " to " + std::to_string(values->max) +
                         " and reaches " + std::to_string(outside) + ", outside the selected range " + range +
                         ", on an iteration that always executes this select: an out-of-range read returns X and an out-of-range write is lost",
                         {.severity = Severity::Error, .confidence = Confidence::Established, .proof = ProofState::NotApplicable,
                          .nextStep = "Correct the loop bound or size the array to cover every iteration.", .signals = {index}});
                return;
            }
            // (b): not an iteration the select always runs. The range decides what kind of finding this is.
            const auto at = e.selector().sourceRange.start();
            const auto iv = vr.of(e.selector());
            const auto isIn = [&](const Interval& v) { return v.lo >= bounds.lower() && v.hi <= bounds.upper(); };
            const bool wrapped = vr.wraps(e.selector());
            if (iv && isIn(*iv) && !wrapped) return;   // every value the index can take is inside the range
            const auto full = e.selector().type ? typeRange(*e.selector().type) : std::nullopt;
            const bool outside = iv && !isIn(*iv);
            if (vr.determined(e.selector())) {
                // Constants and loop variables alone decide the index: no input is involved, so this is no input contract.
                if (outside && !(full && *iv == *full)) {
                    const Wide first = iv->hi > bounds.upper() ? std::max<Wide>(iv->lo, Wide(bounds.upper()) + 1)
                                                                : std::min<Wide>(iv->hi, Wide(bounds.lower()) - 1);
                    l.report(at,
                             "index '" + index + "' takes values " + toString(*iv) + " and reaches " + toString(first) +
                             ", outside the selected range " + range +
                             ", on an iteration this select may run (it is under a condition, a loop exit or an inner loop that is not read): an out-of-range read returns X and an out-of-range write is lost",
                             {.confidence = Confidence::Heuristic,
                              .nextStep = "Correct the loop bound or size the array to cover every iteration, or make the guard that skips the overrun explicit.",
                              .signals = {index}});
                    return;
                }
                l.report(at, "index '" + index + "' is fixed by constants and loop counters but its range is not computed (an unmodelled operation or a wrapping counter): whether it stays inside the selected range " + range + " is not established",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Index with the loop variable directly, or size the array to cover the whole range of the expression.",
                          .signals = {index}});
                return;
            }
            l.report(at,
                     "index '" + index + "' " + (iv ? "can take values " + toString(*iv) : std::string("has no bounded range")) +
                     ", outside the selected range " + range +
                     ": an out-of-range read returns X and an out-of-range write is lost, unless the design keeps the index in range",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Size the array to cover every index value, or clamp the index and state the range the inputs keep.",
                      .signals = {index}, .obligation = "index-input-contract"});
        }
        // An indexed part-select `v[b +: W]` / `v[b -: W]` with a run-time base: the bits it selects are base .. base+W-1 (or base-W+1 .. base),
        // and the base's range decides whether they stay inside the object. A simple range select has constant bounds, which slang checks.
        void handle(const RangeSelectExpression& e) {
            visitDefault(e);
            const auto kind = e.getSelectionKind();
            if (kind == slang::ast::RangeSelectionKind::Simple) return;
            const Type& object = e.value().type->getCanonicalType();
            if (dead || off || (!optIn && vr.determined(e.left())) || !l.isUser(e.sourceRange.start()) || !object.hasFixedRange() || OperandRanges::constant(l, e.left())) return;
            const auto width = lint::constantInt(l, e.right());
            if (!width || *width <= 0) return;
            l.count();
            const auto bounds = object.getFixedRange();
            const Wide w = *width, lowest = bounds.lower(), highest = bounds.upper();
            const bool up = kind == slang::ast::RangeSelectionKind::IndexedUp;
            const auto at = e.left().sourceRange.start();
            const auto iv = vr.of(e.left());
            const auto selects = [&](const Interval& b) { return up ? Interval{b.lo, b.hi + w - 1} : Interval{b.lo - w + 1, b.hi}; };
            const auto isIn = [&](const Interval& v) { return v.lo >= lowest && v.hi <= highest; };
            if (iv && isIn(selects(*iv)) && !vr.wraps(e.left())) return;
            const std::string base = operandText(e.left(), l);
            const std::string range = "[" + std::to_string(bounds.lower()) + ":" + std::to_string(bounds.upper()) + "]";
            const std::string how = std::string(up ? "+:" : "-:") + std::to_string(*width);
            if (vr.determined(e.left())) {
                const auto full = e.left().type ? typeRange(*e.left().type) : std::nullopt;
                if (iv && !isIn(selects(*iv)) && !(full && *iv == *full)) {
                    const auto reach = selects(*iv);
                    l.report(at,
                             "indexed part-select base '" + base + "' takes values " + toString(*iv) + ", so '" + how + "' reaches " +
                             toString(reach.hi > highest ? reach.hi : reach.lo) + ", outside the selected range " + range +
                             ": an out-of-range bit reads as X and an out-of-range write is lost",
                             {.confidence = Confidence::Heuristic,
                              .nextStep = "Correct the loop bound or size the vector to cover every select, or make the guard that skips the overrun explicit.",
                              .signals = {base}});
                    return;
                }
                l.report(at, "indexed part-select base '" + base + "' is fixed by constants and loop counters but its range is not computed: whether '" + how + "' stays inside " + range + " is not established",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Index with the loop variable directly, or size the vector to cover the whole range of the expression.",
                          .signals = {base}});
                return;
            }
            l.report(at,
                     "indexed part-select base '" + base + "' " + (iv ? "can take values " + toString(*iv) + ", so '" + how + "' reaches " + toString(selects(*iv).hi > highest ? selects(*iv).hi : selects(*iv).lo)
                                                                       : std::string("has no bounded range")) +
                     ", outside the selected range " + range + ": an out-of-range bit reads as X and an out-of-range write is lost, unless the design keeps the base in range",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Size the vector to cover every base value, or clamp the base and state the range the inputs keep.",
                      .signals = {base}, .obligation = "index-input-contract"});
        }
    };
    // (b), an index not proven to stay in range: by default the ones a free input decides (an obligation: the type allows values outside
    // the object and nothing narrows them); `dynamic_index=true` adds those fixed by constants and loop counters, `=false` turns (b) off.
    // (c), a loop that provably runs past the range, is always on.
    void check(Lint& l) override {
        const auto called = calledSubroutines(l);
        V v(l, called);
        l.visitHardware(v);
        v.flush();
    }

    // The tasks and functions some code outside a subroutine calls, directly or through other subroutines.
    static std::set<const SubroutineSymbol*> calledSubroutines(Lint& l) {
        struct C : ASTVisitor<C, VisitFlags::AllGood> {
            std::set<const SubroutineSymbol*> roots;
            std::map<const SubroutineSymbol*, std::set<const SubroutineSymbol*>> calls;   // caller -> callees
            const SubroutineSymbol* routine = nullptr;
            void handle(const SubroutineSymbol& r) { auto outer = routine; routine = &r; visitDefault(r); routine = outer; }
            void handle(const CallExpression& call) {
                if (!call.isSystemCall())
                    if (const auto* callee = std::get<0>(call.subroutine)) (routine ? calls[routine] : roots).insert(callee);
                visitDefault(call);
            }
            // A call in a branch a constant condition excludes never runs, so it does not make its callee called; neither does
            // one in a generate block that is not elaborated, or in the operand of a short-circuit that a constant skips.
            Lint* lint = nullptr;
            void handle(const GenerateBlockSymbol& g) { if (!g.isUninstantiated) visitDefault(g); }
            void handle(const BinaryExpression& e) {
                const bool isAnd = e.op == BinaryOperator::LogicalAnd, isOr = e.op == BinaryOperator::LogicalOr;
                if (isAnd || isOr) {
                    auto v = lint->evalConst(e.left());
                    if (v && (isAnd ? v.isFalse() : v.isTrue())) { e.left().visit(*this); return; }
                }
                visitDefault(e);
            }
            void handle(const ConditionalStatement& s) {
                auto [anyFalse, allTrue] = constantConditions(*lint, s.conditions);
                if (!anyFalse && !allTrue) { visitDefault(s); return; }
                for (const auto& c : s.conditions) { c.expr->visit(*this); if (c.pattern) c.pattern->visit(*this); }
                if (!anyFalse) s.ifTrue.visit(*this);
                if (s.ifFalse && !allTrue) s.ifFalse->visit(*this);
            }
            void handle(const ConditionalExpression& e) {
                auto [anyFalse, allTrue] = constantConditions(*lint, e.conditions);
                if (!anyFalse && !allTrue) { visitDefault(e); return; }
                for (const auto& c : e.conditions) { c.expr->visit(*this); if (c.pattern) c.pattern->visit(*this); }
                if (!anyFalse) e.left().visit(*this);
                if (!allTrue) e.right().visit(*this);
            }
            void handle(const CaseStatement& s) {
                slang::ast::ASTContext ac(lint->compilation().getRoot(), slang::ast::LookupLocation::max);
                slang::ast::EvalContext ec(ac);
                auto [taken, known] = s.getKnownBranch(ec);
                if (!known) { visitDefault(s); return; }
                s.expr.visit(*this);
                for (const auto& item : s.items) {
                    for (auto e : item.expressions) e->visit(*this);
                    if (item.stmt.get() == taken) item.stmt->visit(*this);
                }
                if (s.defaultCase && s.defaultCase == taken) s.defaultCase->visit(*this);
            }
        } c;
        c.lint = &l;
        l.visitHardware(c);
        std::set<const SubroutineSymbol*> seen;
        std::vector<const SubroutineSymbol*> pending(c.roots.begin(), c.roots.end());
        while (!pending.empty()) {
            auto* r = pending.back(); pending.pop_back();
            if (!seen.insert(r).second) continue;
            if (auto it = c.calls.find(r); it != c.calls.end()) pending.insert(pending.end(), it->second.begin(), it->second.end());
        }
        return seen;
    }
};

// LNT-037, non-constant part: a divisor not shown to be non-zero. A constant zero divisor is slang's (LNT-037 above).
class DivisorRule final : public LintRule {
public:
    DivisorRule() : LintRule("LNT-037", "", true) {}

private:
    // A signal, port or variable of a module or generate block: state of the design, not a subroutine's formal or local.
    struct ReadsDesignState : ASTVisitor<ReadsDesignState, VisitFlags::AllGood> {
        bool found = false;
        void handle(const NamedValueExpression& e) {
            const auto k = e.symbol.kind;
            if (k != SymbolKind::Net && k != SymbolKind::Variable && k != SymbolKind::Port) return;
            const auto* scope = e.symbol.getParentScope();
            const auto sk = scope ? scope->asSymbol().kind : SymbolKind::Unknown;
            if (sk == SymbolKind::InstanceBody || sk == SymbolKind::GenerateBlock) found = true;
        }
        void handle(const HierarchicalValueExpression&) { found = true; }
        // A call returns what its body computes: the body's reads of design state are the divisor's, and a body that cannot be followed
        // (an imported or otherwise bodiless routine, a method of a class object, a recursion already being followed is not new state) counts as design state.
        std::set<const SubroutineSymbol*> followed;
        void handle(const CallExpression& c) {
            if (!c.isSystemCall()) {
                const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
                if (!sub || !*sub) found = true;
                else if ((*sub)->flags.has(MethodFlags::DPIImport) || (*sub)->getBody().kind == StatementKind::Invalid) found = true;
                else if (followed.insert(*sub).second) (*sub)->getBody().visit(*this);
            }
            visitDefault(c);
        }
    };
    struct V : RangedVisitor<V> {
        using RangedVisitor<V>::handle;
        bool optIn;   // dynamic_divisor=true: also divisors fixed by constants and loop counters, and subroutine formals
        V(Lint& lint, bool opt) : RangedVisitor<V>(lint), optIn(opt) {}
        void handle(const BinaryExpression& e) {
            visitDefault(e);
            if (e.op != BinaryOperator::Divide && e.op != BinaryOperator::Mod) return;
            if (!l.isUser(e.sourceRange.start()) || !e.right().type->isIntegral() || lint::constantInt(l, e.right())) return;
            if (!optIn) {
                // by default only a divisor that reads design state is an obligation: a determined one or a subroutine's formal is opt-in
                if (ranges.determined(e.right())) return;
                ReadsDesignState reads;
                e.right().visit(reads);
                if (!reads.found) return;
            }
            l.count();
            const auto values = ranges.of(e.right());
            if (values && (values->lo > 0 || values->hi < 0)) return;
            const std::string divisor = operandText(e.right(), l);
            const auto at = e.right().sourceRange.start();
            if (ranges.determined(e.right())) {
                // Constants and loop counters alone decide the divisor: no input contract is involved.
                const auto full = typeRange(*e.right().type);
                if (values && !(full && *values == *full)) {
                    l.report(at,
                             "divisor '" + divisor + "' takes values " + toString(*values) + " and is zero in some iteration: a zero divisor makes the result X, and synthesis gives it no defined value",
                             {.confidence = Confidence::Heuristic,
                              .nextStep = "Start the loop above zero, or guard the division against the zero iteration.",
                              .signals = {divisor}});
                    return;
                }
                l.report(at, "divisor '" + divisor + "' is fixed by constants and loop counters but its range is not computed: whether it can be zero is not established",
                         {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                          .nextStep = "Divide by the loop variable directly, or guard the division against a zero divisor.",
                          .signals = {divisor}});
                return;
            }
            l.report(at,
                     "divisor '" + divisor + "' is not shown to be non-zero: a zero divisor makes the result X, and synthesis gives it no defined value",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Guard the division against a zero divisor, or state the input contract that excludes zero.",
                      .signals = {divisor}, .obligation = "divisor-input-contract"});
        }
    };
    // A divisor read from design state is an obligation by default (an input contract is not lint's to decide, but never silent);
    // the opt-in adds the divisors constants and loop counters decide and the formals of subroutines.
    void check(Lint& l) override {
        const bool optIn = l.option("dynamic_divisor", "false") == "true";
        visitDesign<V>(l, optIn);
    }
};

std::vector<std::unique_ptr<Rule>> makeLintWidthRules() {
    std::vector<std::unique_ptr<Rule>> v;
    for (auto k : {LiteralSizeRule::Overflow, LiteralSizeRule::Narrowing, LiteralSizeRule::UnsizedCompare})
        v.push_back(std::make_unique<LiteralSizeRule>(k));
    v.push_back(std::make_unique<ConditionalRule>());
    v.push_back(std::make_unique<UnsizedShiftRule>());
    v.push_back(std::make_unique<ArithmeticRule>());
    v.push_back(std::make_unique<WraparoundRule>());
    v.push_back(std::make_unique<ConstantConcatRule>());
    v.push_back(std::make_unique<ParamTruncationRule>());
    v.push_back(std::make_unique<IndexRangeRule>());
    v.push_back(std::make_unique<DivisorRule>());
    return v;
}

} // namespace sverity
