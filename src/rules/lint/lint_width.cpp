// Native AST lint: width rules. LNT-026 sized-literal fit and narrowing, LNT-034 conditional arms, LNT-035 unsized
// shift operands, LNT-036 arithmetic truncation, LNT-055 unsigned difference below zero, LNT-056 constant concatenations and LNT-057
// parameter-dependent truncation. Slang's implicit width/sign diagnostics (LNT-004/005), shift-count checks (LNT-026),
// signed-logical-shift (LNT-035) and unsized-in-concat (LNT-056) are not repeated here.
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/numeric/SVInt.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/syntax/AllSyntax.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;

using OptWidth = std::optional<bitwidth_t>;

// The operand as written: implicit (assignment or operand-propagation) conversions removed.
const Expression& unwrap(const Expression& e) {
    const Expression* p = &e;
    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit())
        p = &p->as<ConversionExpression>().operand();
    return *p;
}

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
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
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
                    const Expression& lit = unwrap(side == 0 ? b.left() : b.right());
                    const Expression& other = unwrap(side == 0 ? b.right() : b.left());
                    auto w = widthOf(other);
                    if (lit.kind == ExpressionKind::IntegerLiteral && lit.as<IntegerLiteral>().isDeclaredUnsized && w && *w > 32)
                        reportIn(l, path, b.sourceRange.start(),
                                 "unsized literal compared with a " + std::to_string(*w) +
                                     "-bit operand: the literal is 32 bits before extension",
                                 {.severity = Severity::Info, .confidence = Confidence::Heuristic,
                                  .nextStep = "Use a sized literal of the operand's width (e.g. 64'h...) to make the comparison width explicit."});
                }
            }
            visitDefault(b);
        }
        void handle(const AssignmentExpression& a) {
            const Expression& rhs = unwrap(a.right());
            auto tw = widthOf(a.left());
            if (kind == Narrowing && !a.isCompound() && tw && rhs.kind == ExpressionKind::IntegerLiteral && l.isUser(a.sourceRange.start())) {
                l.count();
                SVInt v = rhs.as<IntegerLiteral>().getValue();
                if (!v.hasUnknown() && *tw < v.getBitWidth()) {
                    // The value survives truncation if it reads back unchanged under the target's signedness or the
                    // literal's own (e.g. -1 into a 4-bit unsigned keeps its all-ones pattern and is not reported).
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
        l.compilation().getRoot().visit(v);
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

    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ConditionalExpression& c) {
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                check(c);
            }
            visitDefault(c);
        }
        void check(const ConditionalExpression& c) {
            Arm a = arm(c.left()), b = arm(c.right());
            if (a.fill || b.fill || !a.width || !b.width) return;
            const bool wdiff = *a.width != *b.width;
            // Unsized (decimal or based) literals take a fixed 32-bit width and are left to the info case below.
            const bool widthWarn = wdiff && !a.unsized && !b.unsized;
            const bool signWarn = a.isSigned() != b.isSigned() &&
                                  ((a.isSigned() && (!a.literal || a.negative)) || (b.isSigned() && (!b.literal || b.negative)));
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
// LNT-035: unsized literal shifted in a self-determined position that is wider than 32 bits, and signed left shifts
// of non-constant operands whose context widens them.
class UnsizedShiftRule final : public LintRule {
public:
    UnsizedShiftRule() : LintRule("LNT-035", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        OptWidth target;   // width of the innermost enclosing assignment target
        explicit V(Lint& lint) : l(lint) {}
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
        // Operands of a concatenation are self-determined: an unsized left operand of a shift stays 32 bits there.
        void selfDetermined(const Expression& op, OptWidth container) {
            if (!container || op.kind != ExpressionKind::BinaryOp) return;
            auto& s = op.as<BinaryExpression>();
            if (!isLeftShift(s.op)) return;
            const Expression& lit = unwrap(s.left());
            auto own = widthOf(op);
            if (lit.kind != ExpressionKind::IntegerLiteral || !lit.as<IntegerLiteral>().isDeclaredUnsized || !own || *own > 32)
                return;
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
class ArithmeticRule final : public LintRule {
public:
    ArithmeticRule() : LintRule("LNT-036") {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        bool modular = false;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const BinaryExpression& b) {
            if (l.isUser(b.sourceRange.start())) {
                if (b.op == BinaryOperator::Multiply) product(b);
            }
            visitDefault(b);
        }
        void product(const BinaryExpression& b) {
            auto ew = widthOf(b);
            auto lw = operandBits(l, unwrap(b.left()));
            auto rw = operandBits(l, unwrap(b.right()));
            if (!ew || !lw || !rw) return;
            l.count();
            if (*ew < *lw + *rw)
                reportIn(l, path, b.sourceRange.start(),
                         "product truncated: " + std::to_string(*lw) + "-bit and " + std::to_string(*rw) +
                             "-bit operands are evaluated at " + std::to_string(*ew) + " bits, so the upper bits are lost",
                         {.confidence = Confidence::Heuristic,
                          .nextStep = "Widen the target or cast one operand to the full product width if the full product is intended.",
                          .identityKey = std::to_string(*lw) + ":" + std::to_string(*rw) + ":" + std::to_string(*ew)});
        }
        void handle(const ConcatenationExpression& c) {
            for (auto* op : c.operands()) {
                if (op->kind != ExpressionKind::BinaryOp) continue;
                auto& s = op->as<BinaryExpression>();
                if (s.op != BinaryOperator::Add && s.op != BinaryOperator::Subtract) continue;
                if (!l.isUser(s.sourceRange.start())) continue;
                auto ew = widthOf(*op);
                auto lw = widthOf(unwrap(s.left())), rw = widthOf(unwrap(s.right()));
                if (!ew || !lw || !rw || isConst(l, unwrap(s.left())) || isConst(l, unwrap(s.right()))) continue;
                l.count();
                if (*ew == std::max(*lw, *rw))
                    reportIn(l, path, s.sourceRange.start(),
                             "sum inside a concatenation is evaluated at " + std::to_string(*ew) +
                                 " bits, so its carry bit is lost",
                             {.confidence = Confidence::Heuristic,
                              .nextStep = "Widen an operand before the concatenation (e.g. {1'b0, a} + {1'b0, b}) if the carry is needed."});
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
        l.compilation().getRoot().visit(v);
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
        for (uint32_t i = 0; i < v.getBitWidth(); i++)
            if (bool(v[int32_t(i)]) != one) return false;
        return v.getBitWidth() > 0;
    }

    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
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
                     (b.op == BinaryOperator::LessThanEqual && isConst(l, rhs) && l.evalConst(rhs).integer().isSigned() &&
                      allBits(l.evalConst(rhs).integer(), true)))) {
                    l.count();
                    reportIn(l, path, b.sourceRange.start(),
                             "unsigned difference compared below zero: the subtraction wraps and the comparison is never true",
                             {.nextStep = "Compare the operands before subtracting, or use a signed type for the difference."});
                }
            }
            visitDefault(b);
        }
    };

    void check(Lint& l) override {
        V v(l);
        l.compilation().getRoot().visit(v);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-056: constant concatenations/replications whose width differs from the assignment target, and sized literals
// that overflow their size inside a concatenation or replication.
class ConstantConcatRule final : public LintRule {
public:
    ConstantConcatRule() : LintRule("LNT-056", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
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
        void handle(const AssignmentExpression& a) {
            const Expression& rhs = unwrap(a.right());
            auto tw = widthOf(a.left());
            if (!a.isCompound() && tw && (rhs.kind == ExpressionKind::Concatenation || rhs.kind == ExpressionKind::Replication) &&
                l.isUser(a.sourceRange.start())) {
                auto rw = widthOf(rhs);
                if (rw && isConst(l, rhs)) {
                    l.count();
                    if (*rw != *tw)
                        reportIn(l, path, rhs.sourceRange.start(),
                                 "constant concatenation is " + std::to_string(*rw) + " bits but the target is " +
                                     std::to_string(*tw) + " bits",
                                 {.nextStep = "Pad or trim the constant so its width matches the target."});
                }
            }
            visitDefault(a);
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
    ParamTruncationRule() : LintRule("LNT-057", "only direct parameter references in the declared dimension are recognised; "
                                                "widths declared through a typedef or a nested type are not classified") {}

private:
    struct Names : ASTVisitor<Names, VisitFlags::AllGood> {
        std::vector<const Symbol*> syms;
        void handle(const NamedValueExpression& e) { syms.push_back(&e.symbol); }
        void handle(const HierarchicalValueExpression& e) { syms.push_back(&e.symbol); }
    };

    static bool idStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
    static bool idChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

    // The declared type of `s` names a parameter (or type parameter) in its scope chain. nullopt: text unavailable.
    static std::optional<bool> paramDependent(const Lint& l, const Symbol& s) {
        const DeclaredType* dt = s.getDeclaredType();
        const syntax::DataTypeSyntax* ts = dt ? dt->getTypeSyntax() : nullptr;
        if (!ts) return false;
        auto text = sourceText(l, ts->sourceRange());
        if (!text) return std::nullopt;
        for (size_t i = 0; i < text->size();) {
            if (!idStart((*text)[i])) { i++; continue; }
            size_t j = i;
            while (j < text->size() && idChar((*text)[j])) j++;
            std::string_view id = text->substr(i, j - i);
            for (const Scope* sc = s.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
                if (const Symbol* hit = sc->find(id)) {
                    if (hit->kind == SymbolKind::Parameter || hit->kind == SymbolKind::TypeParameter) return true;
                    break;
                }
            i = j;
        }
        return false;
    }

    // The object an operand names: the first variable, net or port (parameters used in indices are skipped).
    static const Symbol* firstObject(const std::vector<const Symbol*>& syms) {
        for (auto* s : syms)
            if (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Net || s->kind == SymbolKind::Port) return s;
        return nullptr;
    }

    static std::string paramValues(const InstanceBodySymbol* body) {
        if (!body) return "parameters of the enclosing package or unit";
        std::string s;
        for (auto& p : body->membersOfType<ParameterSymbol>()) {
            if (p.isLocalParam()) continue;
            s += (s.empty() ? "" : ", ") + std::string(p.name) + "=" + p.getValue().toString();
        }
        return s.empty() ? "no parameters" : s;
    }

    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        std::string path;   // hierarchical path of the instance body being visited
        void handle(const InstanceBodySymbol& b) {
            auto saved = path;
            path = b.getHierarchicalPath();
            visitDefault(b);
            path = saved;
        }
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}

        // Reports a truncation of `from` bits into `to` bits when either side's declared width depends on a parameter.
        void truncation(slang::SourceLocation at, const Symbol* side, const Symbol* other, bitwidth_t from, bitwidth_t to,
                        const InstanceBodySymbol* body, const std::string& what) {
            l.count();
            std::optional<bool> p1 = side ? paramDependent(l, *side) : false;
            std::optional<bool> p2 = other ? paramDependent(l, *other) : false;
            if (!p1 || !p2) {
                reportIn(l, path, at, what + " truncates " + std::to_string(from) + " to " + std::to_string(to) +
                                 " bits; whether the widths depend on a parameter could not be determined",
                         {.confidence = Confidence::Unresolved, .nextStep = "Declare the widths in the source text so the parameter dependence can be checked.",
                          .hierarchy = body ? body->getHierarchicalPath() : ""});
                return;
            }
            if (!*p1 && !*p2) return;
            reportIn(l, path, at, what + " truncates " + std::to_string(from) + " to " + std::to_string(to) +
                             " bits and a width is set by a parameter (" + paramValues(body) + ")",
                     {.confidence = Confidence::Heuristic,
                      .nextStep = "Check the truncation for every parameter value this module can take, or make it explicit with a cast.",
                      .hierarchy = body ? body->getHierarchicalPath() : ""});
        }

        void handle(const AssignmentExpression& a) {
            if (!a.isCompound() && !a.isLValueArg() && l.isUser(a.sourceRange.start())) {
                auto lw = widthOf(a.left());
                const Expression& r = unwrap(a.right());
                auto rw = neededWidth(l, r);
                if (lw && rw && *rw > *lw) {
                    Names ln, rn;
                    a.left().visit(ln);
                    r.visit(rn);
                    const Symbol* ls = firstObject(ln.syms);
                    const Symbol* rs = firstObject(rn.syms);
                    truncation(a.sourceRange.start(), ls, rs, *rw, *lw, ls ? enclosingBody(*ls) : nullptr, "assignment");
                }
            }
            visitDefault(a);
        }

        void handle(const InstanceSymbol& inst) {
            for (auto* pc : inst.getPortConnections()) {
                const Expression* ex = pc->getExpression();
                if (!ex || pc->port.kind != SymbolKind::Port) continue;
                auto& port = pc->port.as<PortSymbol>();
                auto pw = port.getType().isIntegral() ? OptWidth(port.getType().getBitWidth()) : std::nullopt;
                const Expression& actual = unwrap(*ex);
                if (!l.isUser(actual.sourceRange.start())) continue;
                auto aw = widthOf(actual);
                if (!pw || !aw || port.direction == ArgumentDirection::InOut || port.direction == ArgumentDirection::Ref)
                    continue;
                bool input = port.direction == ArgumentDirection::In;
                const uint32_t pwv = *pw, awv = *aw;
                if (input ? awv <= pwv : pwv <= awv) continue;
                Names an;
                actual.visit(an);
                const Symbol* as = firstObject(an.syms);
                std::string what = std::string(input ? "input" : "output") + " port '" + std::string(port.name) + "' of '" +
                                   std::string(inst.name) + "'";
                truncation(actual.sourceRange.start(), port.internalSymbol ? port.internalSymbol : &port, as, input ? awv : pwv, input ? pwv : awv, &inst.body, what);
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
struct ValueRange { int64_t min = 0, max = 0; };

class OperandRanges {
public:
    void enter(const ForLoopStatement& loop, Lint& l) {
        std::vector<std::pair<const ValueSymbol*, int64_t>> starts;
        for (auto* v : loop.loopVars)
            if (auto* init = v->getInitializer()) starts.push_back({v, constant(l, *init).value_or(INT64_MIN)});
        for (auto* e : loop.initializers)
            if (e->kind == ExpressionKind::Assignment) {
                const auto& a = e->as<AssignmentExpression>();
                if (a.left().kind == ExpressionKind::NamedValue)
                    starts.push_back({&a.left().as<NamedValueExpression>().symbol, constant(l, a.right()).value_or(INT64_MIN)});
            }
        if (!loop.stopExpr || loop.stopExpr->kind != ExpressionKind::BinaryOp) return;
        const auto& stop = loop.stopExpr->as<BinaryExpression>();
        if (stop.left().kind != ExpressionKind::NamedValue) return;
        const auto* var = &stop.left().as<NamedValueExpression>().symbol;
        auto bound = constant(l, stop.right());
        // Only a loop that steps towards its bound, and whose body never writes the variable, stays inside [start, bound].
        const int direction = stepDirection(loop, var, l);
        if (direction == 0 || writes(loop.body, var)) return;
        for (auto [symbol, start] : starts) {
            if (symbol != var || !bound || start == INT64_MIN) continue;
            if (direction > 0 && stop.op == BinaryOperator::LessThan && start < *bound) loops_[var] = {start, *bound - 1};
            else if (direction > 0 && stop.op == BinaryOperator::LessThanEqual && start <= *bound) loops_[var] = {start, *bound};
            else if (direction < 0 && stop.op == BinaryOperator::GreaterThan && start > *bound) loops_[var] = {*bound + 1, start};
            else if (direction < 0 && stop.op == BinaryOperator::GreaterThanEqual && start >= *bound) loops_[var] = {*bound, start};
        }
    }
    /// +1 when the loop's only step moves `var` up by a positive constant, -1 down, 0 otherwise.
    static int stepDirection(const ForLoopStatement& loop, const ValueSymbol* var, Lint& l) {
        if (loop.steps.size() != 1) return 0;
        const Expression& step = *loop.steps[0];
        const auto isVar = [&](const Expression& e) { return e.kind == ExpressionKind::NamedValue && &e.as<NamedValueExpression>().symbol == var; };
        if (step.kind == ExpressionKind::UnaryOp) {
            const auto& u = step.as<UnaryExpression>();
            if (!isVar(u.operand())) return 0;
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement) return 1;
            if (u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) return -1;
            return 0;
        }
        if (step.kind != ExpressionKind::Assignment) return 0;
        const auto& a = step.as<AssignmentExpression>();
        if (!isVar(a.left()) || !a.op) return 0;
        // slang stores `i += c` with its right-hand side as `<lvalue reference> + c`.
        const Expression* rhs = &unwrap(a.right());
        if (rhs->kind == ExpressionKind::BinaryOp) {
            const auto& left = unwrap(rhs->as<BinaryExpression>().left());
            if (left.kind == ExpressionKind::LValueReference || isVar(left)) rhs = &rhs->as<BinaryExpression>().right();
        }
        auto amount = constant(l, *rhs);
        if (!amount || *amount <= 0) return 0;
        return *a.op == BinaryOperator::Add ? 1 : *a.op == BinaryOperator::Subtract ? -1 : 0;
    }
    static bool writes(const Statement& body, const ValueSymbol* var) {
        struct W : ASTVisitor<W, VisitFlags::AllGood> {
            const ValueSymbol* var; bool found = false;
            explicit W(const ValueSymbol* v) : var(v) {}
            bool names(const Expression& e) const {
                bool hit = false;
                e.visitSymbolReferences([&](const Expression&, const Symbol& s) { hit = hit || &s == var; });
                return hit;
            }
            void handle(const AssignmentExpression& a) { found = found || names(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                const bool step = u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                                  u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement;
                found = found || (step && names(u.operand()));
                visitDefault(u);
            }
        } w(var);
        body.visit(w);
        return w.found;
    }
    void leave(const ForLoopStatement& loop) {
        for (auto* v : loop.loopVars) loops_.erase(v);
        if (loop.stopExpr && loop.stopExpr->kind == ExpressionKind::BinaryOp) {
            const auto& left = loop.stopExpr->as<BinaryExpression>().left();
            if (left.kind == ExpressionKind::NamedValue) loops_.erase(&left.as<NamedValueExpression>().symbol);
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
    static std::optional<int64_t> constant(Lint& l, const Expression& e) {
        auto cv = l.evalConst(e);
        if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
        return cv.integer().as<int64_t>();
    }

private:
    std::unordered_map<const ValueSymbol*, ValueRange> loops_;
};

std::string operandText(const Expression& e, Lint& l) {
    const auto& operand = unwrap(e);
    if (operand.kind == ExpressionKind::NamedValue) return std::string(operand.as<NamedValueExpression>().symbol.name);
    return std::string(l.sm().getSourceText(operand.sourceRange.start().buffer()).substr(
        operand.sourceRange.start().offset(), operand.sourceRange.end().offset() - operand.sourceRange.start().offset()));
}

// LNT-006, non-constant part: an index whose values reach outside the array. Constant indexes are slang's (LNT-006 above).
class IndexRangeRule final : public LintRule {
public:
    IndexRangeRule() : LintRule("LNT-006", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        OperandRanges ranges;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ForLoopStatement& s) { ranges.enter(s, l); visitDefault(s); ranges.leave(s); }
        void handle(const ElementSelectExpression& e) {
            visitDefault(e);
            const Type& array = e.value().type->getCanonicalType();
            if (!l.isUser(e.sourceRange.start()) || !array.hasFixedRange() || OperandRanges::constant(l, e.selector())) return;
            l.count();
            const auto bounds = array.getFixedRange();
            auto values = ranges.of(e.selector());
            if (values && values->min >= bounds.lower() && values->max <= bounds.upper()) return;
            const std::string index = operandText(e.selector(), l);
            l.report(e.selector().sourceRange.start(),
                     "index '" + index + "' " + (values ? "can take values " + std::to_string(values->min) + " to " + std::to_string(values->max)
                                                         : "has no bounded range") +
                     ", outside the selected range [" + std::to_string(bounds.lower()) + ":" + std::to_string(bounds.upper()) +
                     "]: an out-of-range read returns X and an out-of-range write is lost, unless the design keeps the index in range",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Size the array to cover every index value, or clamp the index and state the range the inputs keep.",
                      .signals = {index}});
        }
    };
    // Opt-in: whether a free operand stays in range is an input contract that simulation or formal checks, not lint.
    void check(Lint& l) override { if (l.option("dynamic_index", "false") == "true") visitDesign<V>(l); }
};

// LNT-037, non-constant part: a divisor not shown to be non-zero. A constant zero divisor is slang's (LNT-037 above).
class DivisorRule final : public LintRule {
public:
    DivisorRule() : LintRule("LNT-037", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        OperandRanges ranges;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ForLoopStatement& s) { ranges.enter(s, l); visitDefault(s); ranges.leave(s); }
        void handle(const BinaryExpression& e) {
            visitDefault(e);
            if (e.op != BinaryOperator::Divide && e.op != BinaryOperator::Mod) return;
            if (!l.isUser(e.sourceRange.start()) || !e.right().type->isIntegral() || OperandRanges::constant(l, e.right())) return;
            l.count();
            auto values = ranges.of(e.right());
            if (values && (values->min > 0 || values->max < 0)) return;
            const std::string divisor = operandText(e.right(), l);
            l.report(e.right().sourceRange.start(),
                     "divisor '" + divisor + "' is not shown to be non-zero: a zero divisor makes the result X, and synthesis gives it no defined value",
                     {.confidence = Confidence::Unresolved, .proof = ProofState::NotAttempted,
                      .nextStep = "Guard the division against a zero divisor, or state the input contract that excludes zero.",
                      .signals = {divisor}});
        }
    };
    // Opt-in: whether a free operand stays in range is an input contract that simulation or formal checks, not lint.
    void check(Lint& l) override { if (l.option("dynamic_divisor", "false") == "true") visitDesign<V>(l); }
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
