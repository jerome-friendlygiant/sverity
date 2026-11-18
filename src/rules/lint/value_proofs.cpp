// Value proofs for slang's width and sign findings (LNT-004, LNT-005). slang reports every mix of signed and unsigned operands
// (sign-compare, sign-conversion, arith-op-mismatch) and every implicit width change; a finding is demoted to INFO only when
// the elaborated AST shows that the value is preserved at its site (docs/rules/LNT.md, LNT-004 and LNT-005):
//   a value range (value_range.h) shows each converted operand lies in the type it is converted to;
//   a same-width equality compares bit patterns, whatever the signedness;
//   a manual sign extension {a[W-1], a} (or {{K{a[W-1]}}, a}) is converted to a signed type of width W+K;
//   an operation with a constant operand inside an index or shift count, and a conditional arm widened by its context, lose no bit;
//   a single-bit flag added to a wider operand, or an operation whose narrower operand is a fill literal;
//   a concatenation or replication of values of one enum type that fills a packed array of that enum.
// Everything else stays WARNING. The proofs read the elaborated AST, so they hold per canonical instance body.
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "rules/lint/value_range.h"
#include "rules/rules.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"

namespace sverity {
namespace {

using namespace slang::ast;
using namespace lint;

// Locations are compared as text: the frontend builds the same file:line:column for every finding.
std::string keyOf(const slang::SourceManager& sm, slang::SourceLocation at) {
    if (!at.valid()) return {};
    return sm.getFullPath(at.buffer()).generic_string() + ":" + std::to_string(sm.getLineNumber(at)) + ":" +
           std::to_string(sm.getColumnNumber(at));
}
std::string keyOf(const sverity::SourceLocation& at) {
    if (!at.valid()) return {};
    return at.file + ":" + std::to_string(at.line) + ":" + std::to_string(at.column);
}

bool mixedSign(const Expression& a, const Expression& b) {
    const Expression& x = unwrapImplicit(a);
    const Expression& y = unwrapImplicit(b);
    if (!x.type || !y.type || !x.type->isIntegral() || !y.type->isIntegral()) return false;
    return x.type->isSigned() != y.type->isSigned();
}

// A plain vector or integer type: not an enum, struct or union, whose type is part of what the author meant.
bool plainBits(const Type& t) {
    const auto k = t.getCanonicalType().kind;
    return t.isIntegral() && (k == SymbolKind::PredefinedIntegerType || k == SymbolKind::ScalarType || k == SymbolKind::PackedArrayType);
}

bool isBitwise(BinaryOperator op) {
    return op == BinaryOperator::BinaryAnd || op == BinaryOperator::BinaryOr || op == BinaryOperator::BinaryXor ||
           op == BinaryOperator::BinaryXnor;
}
bool isArith(BinaryOperator op) {
    return op == BinaryOperator::Add || op == BinaryOperator::Subtract || op == BinaryOperator::Multiply ||
           op == BinaryOperator::Divide || op == BinaryOperator::Mod;
}
bool isWrappingOp(BinaryOperator op) {
    return op == BinaryOperator::Add || op == BinaryOperator::Subtract || op == BinaryOperator::Multiply ||
           op == BinaryOperator::LogicalShiftLeft || op == BinaryOperator::ArithmeticShiftLeft;
}
bool isEquality(BinaryOperator op) {
    return op == BinaryOperator::Equality || op == BinaryOperator::Inequality || op == BinaryOperator::CaseEquality ||
           op == BinaryOperator::CaseInequality || op == BinaryOperator::WildcardEquality || op == BinaryOperator::WildcardInequality;
}

bool operandTypesDiffer(const BinaryExpression& e) {
    const Type* x = unwrapImplicit(e.left()).type;
    const Type* y = unwrapImplicit(e.right()).type;
    return x && y && !x->getCanonicalType().isMatching(y->getCanonicalType());
}

enum class Flavor { Sign, Width };   // LNT-005's proofs and LNT-004's

// Walks every canonical instance body once (not a definition nobody instantiates: its parameters have no values, so nothing is
// proven there). For each site slang can report it records whether that site is proven. A location reached through two
// instances with different parameters is proven only when every instance that reaches it is proven.
class ValueProofs : public HardwareVisitor<ValueProofs, VisitFlags::AllCanonical> {
public:
    struct Site {
        bool proven = true;
        std::string sentence;   // the proof of the first instance; empty until one is proven
    };
    explicit ValueProofs(Lint& lint) : l(lint), ranges(lint) {}

    std::map<std::string, Site> sites;   // key: flavor, upstream code "@" location

    void handle(const ForLoopStatement& s) { ranges.visitLoop(*this, s); }

    // The condition of a generate `if` or `case` is evaluated at elaboration and is no node of the procedural AST: slang keeps the bound
    // expression on each block it creates, the unselected ones too (an inactive block is never entered, but its condition was
    // evaluated and judged), so it is read from the scope that holds the blocks.
    void conditions(const Scope& scope) {
        for (const auto& member : scope.members()) {
            if (member.kind != SymbolKind::GenerateBlock) continue;
            const auto& block = member.as<GenerateBlockSymbol>();
            if (block.conditionExpression) block.conditionExpression->visit(*this);
            for (const auto* item : block.caseItemExpressions) item->visit(*this);
        }
    }
    void handle(const InstanceBodySymbol& body) {
        conditions(body);
        visitDefault(body);
    }

    // A parameter override (`.W(A * B)`) is code of the instantiating scope, bound on the body of the child instance. When slang shares
    // one body between instances with equal parameter values, only the canonical one is walked, so the others' overrides are read here.
    void handle(const InstanceSymbol& inst) {
        if (const auto* canonical = inst.getCanonicalBody(); canonical && canonical != &inst.body)
            for (const auto& param : inst.body.membersOfType<ParameterSymbol>())
                if (const auto* init = param.getInitializer()) init->visit(*this);
        visitDefault(inst);
    }
    void handle(const GenerateBlockSymbol& block) {
        conditions(block);
        visitDefault(block);
    }

    // The initializers of an enum's values (`B = A + 1`) hang on the type, which the symbol walk does not enter.
    void handle(const TypeAliasType& alias) {
        const Type& target = alias.targetType.getType().getCanonicalType();
        if (target.isEnum())
            for (const auto& value : target.as<EnumType>().values())
                if (const auto* init = value.getInitializer()) init->visit(*this);
        visitDefault(alias);
    }

    void handle(const BinaryExpression& e) {
        const auto op = e.op;
        if (OpInfo::isComparison(op)) {
            if (mixedSign(e.left(), e.right())) record(Flavor::Sign, "sign-compare", e.opRange.start(), comparisonProof(e));
            else record(Flavor::Sign, "sign-compare", e.opRange.start(), propagatedCompareProof(e));
        }
        if ((isArith(op) || isBitwise(op)) && operandTypesDiffer(e)) {   // slang reports a mismatch only between types that differ
            const char* code = isBitwise(op) ? "bitwise-op-mismatch" : "arith-op-mismatch";
            record(Flavor::Sign, code, e.opRange.start(), operandSignProof(e));
            record(Flavor::Width, code, e.opRange.start(), operandWidthProof(e));
        }
        else if (isArith(op) && e.type && e.type->isIntegral())   // an operand slang retyped into the operation's type
            record(Flavor::Sign, "arith-op-mismatch", e.opRange.start(), propagatedProof(e, "operation"));
        if ((isArith(op) || isBitwise(op)) && !operandTypesDiffer(e) && isFill(e.left()) != isFill(e.right()))   // slang typed the fill in the context's width already
            record(Flavor::Width, isBitwise(op) ? "bitwise-op-mismatch" : "arith-op-mismatch", e.opRange.start(),
                   std::optional<std::string>("Value preserved: a fill literal takes the width of its context, so the operands have the same width where the operation is evaluated"));
        if (OpInfo::isShift(op)) {   // the count is self-determined, like an index
            e.left().visit(*this);
            ++selfDetermined;
            e.right().visit(*this);
            --selfDetermined;
            return;
        }
        visitDefault(e);
    }

    void handle(const ElementSelectExpression& e) {
        e.value().visit(*this);
        ++selfDetermined;
        e.selector().visit(*this);
        --selfDetermined;
    }
    void handle(const RangeSelectExpression& e) {
        e.value().visit(*this);
        ++selfDetermined;
        e.left().visit(*this);
        e.right().visit(*this);
        --selfDetermined;
    }

    void handle(const ConversionExpression& c) {
        if (c.isImplicit() && c.type && c.operand().type && c.type->isIntegral() && c.operand().type->isIntegral() &&
            c.type->isSigned() != c.operand().type->isSigned())
            record(Flavor::Sign, "sign-conversion", c.sourceRange.start(), conversionProof(c));
        if (c.isImplicit() && c.type) recordEnumBag(*c.type, c.operand());
        // A sign extension that an outer conversion then reads as unsigned changes every negative value: only a range proves it.
        if (c.isImplicit() && c.type && c.type->isIntegral() && !c.type->isSigned() && c.operand().type && c.operand().type->isSigned())
            unprovenSignExtensions(c.operand());
        if (c.isImplicit() && c.type && c.operand().type && c.type->isIntegral() && c.operand().type->isIntegral() && widens(c)) {
            const auto proof = extensionProof(c);
            record(Flavor::Width, "width-expand", c.sourceRange.start(), proof);
            record(Flavor::Width, "port-width-expand", c.sourceRange.start(), proof);
        }
        visitDefault(c);
    }

    // slang reports a packed-array-conv at the start of the converted expression; the assignment, parameter or variable that
    // converts it carries no conversion node when the two packed types have one width.
    void handle(const AssignmentExpression& a) {
        if (!a.isCompound() && a.left().type) recordEnumBag(*a.left().type, a.right());
        visitDefault(a);
    }
    void handle(const ParameterSymbol& p) {
        if (const auto* init = p.getInitializer()) recordEnumBag(p.getType(), *init);
        visitDefault(p);
    }
    void handle(const VariableSymbol& v) {
        if (const auto* init = v.getInitializer()) recordEnumBag(v.getType(), *init);
        visitDefault(v);
    }

    // slang checks an arm of a conditional against the context before it propagates the type into it.
    void handle(const ConditionalExpression& c) {
        for (const Expression* arm : {&c.left(), &c.right()})
            if (arm->kind == ExpressionKind::BinaryOp && isWrappingOp(arm->as<BinaryExpression>().op))   // an unproven arm stays a finding, whatever the extension of its first operand says
                record(Flavor::Width, "width-expand", arm->sourceRange.start(), widenedArmProof(*arm));
        visitDefault(c);
    }

private:
    void record(Flavor flavor, const char* code, slang::SourceLocation at, const std::optional<std::string>& proof) {
        const std::string key = keyOf(l.sm(), at);
        if (key.empty()) return;
        Site& site = sites[std::string(flavor == Flavor::Sign ? "sign:" : "width:") + code + "@" + key];
        if (!proof) {
            site.proven = false;
            return;
        }
        if (site.sentence.empty()) site.sentence = *proof;
    }

    std::string name(const Expression& e) const {
        const auto t = ranges.text(e);
        return t.empty() ? "the operand" : "'" + t + "'";
    }
    std::string rangeOf(const Expression& e) const {
        const auto i = ranges.of(e);
        return i ? toString(*i) : "its type's values";
    }

    // Both sides of a comparison are converted to one type; the comparison is the one the author wrote when neither conversion
    // changes a value, or when it only tests bit patterns of one width.
    std::optional<std::string> comparisonProof(const BinaryExpression& e) {
        const Expression& x = unwrapImplicit(e.left());
        const Expression& y = unwrapImplicit(e.right());
        if (isEquality(e.op) && x.type->getBitWidth() == y.type->getBitWidth())
            return "Value preserved: an equality of two " + std::to_string(x.type->getBitWidth()) +
                   "-bit operands compares bit patterns, so their signedness does not change the result";
        if (!ranges.preservedThrough(e.left()) || !ranges.preservedThrough(e.right())) return std::nullopt;
        return "Value preserved: " + name(x) + " lies in " + rangeOf(x) + " and " + name(y) + " in " + rangeOf(y) +
               ", so both convert to the " + e.left().type->toString() + " comparison type unchanged";
    }

    // slang types an arithmetic operand in the type of the comparison or operation that holds it before this check sees it, so
    // `l >= (N - 2)` or `(A - 2) - U` show unsigned sides although the author wrote an `int`. Such a site is proven when every side
    // that could have been retyped is a value that stays in the lower half of its range, where a signed and an unsigned reading
    // agree (an interval of the expression as typed, which is never narrower than the truth), and every plain operand reaches the
    // common type unchanged. A side whose range is not known to be small is left to the warning.
    std::optional<std::string> propagatedProof(const BinaryExpression& e, const char* kind) {
        std::string said;
        for (const Expression* side : {&e.left(), &e.right()}) {
            const Expression& x = unwrapImplicit(*side);
            if (!x.type || !x.type->isIntegral() || x.type->getBitWidth() == 0 || x.type->getBitWidth() > 63) return std::nullopt;
            const auto value = ranges.of(x);
            if (!value) return std::nullopt;
            const bool leaf = x.kind == ExpressionKind::NamedValue || x.kind == ExpressionKind::ElementSelect ||
                              x.kind == ExpressionKind::RangeSelect || x.kind == ExpressionKind::MemberAccess;
            const bool small = value->lo >= 0 && value->hi < (Wide(1) << (x.type->getBitWidth() - 1));
            if (!small && !(leaf && ranges.preservedThrough(*side))) return std::nullopt;
            said += (said.empty() ? "" : " and ") + name(x) + " in " + rangeOf(x);
        }
        return "Value preserved: " + said + ", so both convert to the " + e.left().type->toString() + " " + kind + " type unchanged";
    }
    std::optional<std::string> propagatedCompareProof(const BinaryExpression& e) { return propagatedProof(e, "comparison"); }

    // slang's arith-op-mismatch / bitwise-op-mismatch for operands of one width and different signedness: the operation is the one
    // the author wrote when both operands survive the conversion to the common type.
    std::optional<std::string> operandSignProof(const BinaryExpression& e) {
        const Expression& x = unwrapImplicit(e.left());
        const Expression& y = unwrapImplicit(e.right());
        // An enumerator or a constant of an enum type is a number at elaboration; only a signal of an enum type is a type the author chose.
        const auto fixedOrPlain = [&](const Expression& operand, const Type& type) {
            return plainBits(type) || (type.getCanonicalType().isEnum() && l.evalConst(operand).isInteger());
        };
        if (!x.type || !y.type || !fixedOrPlain(x, *x.type) || !fixedOrPlain(y, *y.type)) return std::nullopt;
        if (!ranges.preservedThrough(e.left()) || !ranges.preservedThrough(e.right())) return std::nullopt;
        return "Value preserved: " + name(x) + " lies in " + rangeOf(x) + " and " + name(y) + " in " + rangeOf(y) +
               ", so both convert to the " + e.type->toString() + " operation type unchanged";
    }

    // slang's arith-op-mismatch for operands of different widths: inside an index or a shift count (self-determined, so no wider
    // context decides the width) an operation between a signal and a constant that keeps its value in the expression's own width.
    // An operand that is a fill literal ('0, '1, 'x, 'z) or a conditional of such takes the width of its context.
    static bool isFill(const Expression& e) {
        const Expression& x = unwrapImplicit(e);
        if (x.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral) return true;
        if (x.kind == ExpressionKind::ConditionalOp) {
            const auto& c = x.as<ConditionalExpression>();
            return isFill(c.left()) && isFill(c.right());
        }
        return false;
    }

    std::optional<std::string> operandWidthProof(const BinaryExpression& e) {
        // slang's arith-op-mismatch / bitwise-op-mismatch of operands of different widths. A fill literal takes the width of its context.
        // A single-bit unsigned operand of an arithmetic operation is a flag or an increment (`count + bit`): it zero-extends losslessly
        // and nothing else is meant. A wider operand is not judged so: `a + b` of 4 and 8 bits may mean either width, and a narrow
        // operand of a bitwise operation is zero-extended, which masks the upper bits (`data & enable`), the mistake this finding is for.
        if (isFill(e.left()) != isFill(e.right()) && (isBitwise(e.op) || isArith(e.op)))
            return "Value preserved: a fill literal takes the width of its context, so the operands have the same width where the operation is evaluated";
        if (isArith(e.op) && e.type && e.type->isIntegral()) {
            const Expression& a = unwrapImplicit(e.left());
            const Expression& b = unwrapImplicit(e.right());
            const auto flag = [&](const Expression& x) { return x.type && !x.type->isSigned() && x.type->getBitWidth() == 1; };
            if ((flag(a) || flag(b)) && ranges.preservedThrough(e.left()) && ranges.preservedThrough(e.right()))
                return "Value preserved: " + name(a) + " lies in " + rangeOf(a) + " and " + name(b) + " in " + rangeOf(b) +
                       ", so both reach the " + e.type->toString() + " operation type unchanged, a single bit being a flag or an increment";
        }
        if (e.op != BinaryOperator::Add && e.op != BinaryOperator::Subtract && e.op != BinaryOperator::Multiply)
            return std::nullopt;
        const Expression& x = unwrapImplicit(e.left());
        const Expression& y = unwrapImplicit(e.right());
        const auto cx = ranges.of(x), cy = ranges.of(y);
        const bool constX = l.evalConst(x).isInteger(), constY = l.evalConst(y).isInteger();
        if (constX == constY || !cx || !cy || !ranges.wrapFree(e)) return std::nullopt;
        const auto result = ranges.mathematical(e);
        if (!result) return std::nullopt;
        return "Value preserved: " + name(x) + " lies in " + toString(*cx) + " and " + name(y) + " in " + toString(*cy) +
               ", so the result lies in " + toString(*result) + ", inside the " + std::to_string(e.type->getBitWidth()) +
               "-bit expression: extending the narrower operand and the operation lose no bit";
    }

    // An arm of a conditional that is arithmetic is evaluated at the width of its context, not the width of its operands, so the
    // product of two 32-bit values assigned to 64 bits is the full product, as it is outside a conditional.
    std::optional<std::string> widenedArmProof(const Expression& arm) {
        if (arm.kind != ExpressionKind::BinaryOp || !arm.type) return std::nullopt;
        const auto& b = arm.as<BinaryExpression>();
        const bool shiftLeft = b.op == BinaryOperator::LogicalShiftLeft || b.op == BinaryOperator::ArithmeticShiftLeft;
        if (b.op != BinaryOperator::Add && b.op != BinaryOperator::Subtract && b.op != BinaryOperator::Multiply && !shiftLeft)
            return std::nullopt;
        const auto& lt = *unwrapImplicit(b.left()).type;
        const auto& rt = *unwrapImplicit(b.right()).type;
        const auto natural = shiftLeft ? lt.getBitWidth() : std::max(lt.getBitWidth(), rt.getBitWidth());
        if (arm.type->getBitWidth() <= natural || !ranges.wrapFree(arm)) return std::nullopt;
        const auto result = ranges.mathematical(arm);
        if (!result) return std::nullopt;
        return "Value preserved: " + name(arm) + " is evaluated at the " + std::to_string(arm.type->getBitWidth()) +
               "-bit width of its context, as outside a conditional, and its result lies in " + toString(*result) +
               ", so no bit is lost";
    }

    // The sign extensions that make up `e` (itself, or the arms of a conditional): each one whose operand may be negative is not proven.
    void unprovenSignExtensions(const Expression& e, int depth = 0) {
        if (depth > 8) return;
        if (e.kind == ExpressionKind::ConditionalOp) {
            unprovenSignExtensions(e.as<ConditionalExpression>().left(), depth + 1);
            unprovenSignExtensions(e.as<ConditionalExpression>().right(), depth + 1);
            return;
        }
        if (e.kind != ExpressionKind::Conversion) return;
        const auto& inner = e.as<ConversionExpression>();
        if (!inner.isImplicit() || !inner.type || !inner.operand().type || !inner.type->isIntegral() || !inner.operand().type->isIntegral() ||
            !inner.type->isSigned() || !inner.operand().type->isSigned() || !widens(inner))
            return;
        const auto in = ranges.of(inner.operand());
        if (in && in->lo >= 0) return;
        record(Flavor::Width, "width-expand", inner.sourceRange.start(), std::nullopt);
        record(Flavor::Width, "port-width-expand", inner.sourceRange.start(), std::nullopt);
    }
    static bool widens(const ConversionExpression& c) { return c.type->getBitWidth() > c.operand().type->getBitWidth(); }

    // The value the expression computes is what its operands say: no operation wraps, no implicit conversion inside changes a value.
    // An explicit size cast is the author's own and ends the walk.
    bool valueClean(const Expression& e, int depth = 0) const {
        if (depth > 12) return false;
        switch (e.kind) {
            case ExpressionKind::Conversion: {
                const auto& c = e.as<ConversionExpression>();
                // an explicit size cast is the author's; a sign cast of one width only reinterprets what is inside it
                if (!c.isImplicit()) return !c.operand().type || !c.type || c.operand().type->getBitWidth() != c.type->getBitWidth() || valueClean(c.operand(), depth + 1);
                const auto in = ranges.of(c.operand());
                const auto out = c.type ? typeRange(*c.type) : std::nullopt;
                return in && out && in->within(*out) && valueClean(c.operand(), depth + 1);
            }
            case ExpressionKind::BinaryOp: {
                const auto& b = e.as<BinaryExpression>();
                const bool arith = b.op == BinaryOperator::Add || b.op == BinaryOperator::Subtract || b.op == BinaryOperator::Multiply ||
                                   b.op == BinaryOperator::Power || b.op == BinaryOperator::LogicalShiftLeft || b.op == BinaryOperator::ArithmeticShiftLeft;
                if (arith && !ranges.wrapFree(e)) return false;
                if (OpInfo::isComparison(b.op) || b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) return true;
                return valueClean(b.left(), depth + 1) && valueClean(b.right(), depth + 1);
            }
            case ExpressionKind::UnaryOp: {
                const auto& u = e.as<UnaryExpression>();
                if (u.op == UnaryOperator::Minus) return ranges.wrapFree(e) && valueClean(u.operand(), depth + 1);
                if (u.op == UnaryOperator::Plus || u.op == UnaryOperator::BitwiseNot) return valueClean(u.operand(), depth + 1);
                return true;   // a reduction or a logical not yields one bit
            }
            case ExpressionKind::Call: {
                const auto& call = e.as<CallExpression>();
                if (call.isSystemCall() && (call.getSubroutineName() == "$signed" || call.getSubroutineName() == "$unsigned") && call.arguments().size() == 1)
                    return valueClean(*call.arguments()[0], depth + 1);
                return true;
            }
            case ExpressionKind::Concatenation: {
                const auto operands = e.as<ConcatenationExpression>().operands();
                return std::all_of(operands.begin(), operands.end(), [&](const Expression* o) { return valueClean(*o, depth + 1); });
            }
            case ExpressionKind::Replication: return valueClean(e.as<ReplicationExpression>().concat(), depth + 1);
            case ExpressionKind::ConditionalOp: {
                const auto& c = e.as<ConditionalExpression>();
                return valueClean(c.left(), depth + 1) && valueClean(c.right(), depth + 1);
            }
            default: return true;
        }
    }

    // slang's width-expand (and port-width-expand): an implicit extension of an integral value. It is the value the author wrote when the
    // extension carries it unchanged: a zero extension (an unsigned operand), a sign extension into a signed type, or any value whose
    // range lies in the destination, on an operand that is itself clean (see valueClean). A signed operand into an unsigned type
    // changes every negative value, so only its range can prove it.
    std::optional<std::string> extensionProof(const ConversionExpression& c) {
        const Expression& operand = c.operand();
        const Type& from = *operand.type;
        const Type& to = *c.type;
        if (to.getBitWidth() <= from.getBitWidth() || !valueClean(operand)) return std::nullopt;
        std::string how;
        if (!from.isSigned()) how = "zero extension of an unsigned value";
        else if (to.isSigned()) how = "sign extension of a signed value into a signed type";
        else {
            const auto in = ranges.of(operand);
            const auto out = typeRange(to);
            if (!in || !out || !in->within(*out)) return std::nullopt;
            how = "extension of a value that lies in " + toString(*in);
        }
        return "Value preserved: " + name(operand) + " is a " + std::to_string(from.getBitWidth()) + "-bit " + (from.isSigned() ? "signed" : "unsigned") +
               " value and the extension to " + std::to_string(to.getBitWidth()) + " bits is a " + how + ", so its value is unchanged";
    }

    // slang's packed-array-conv from a vector to a packed array of an enum: nothing is reinterpreted when every piece of the
    // vector is itself a value of that enum, laid end to end (`{N{ENUM_VALUE}}`, `{A, B}` of the enum's type).
    static bool enumBag(const Expression& e, const Type& element) {
        const Expression& x = unwrapImplicit(e);
        if (x.type && x.type->getCanonicalType().isMatching(element)) return true;
        if (x.kind == ExpressionKind::Concatenation) {
            const auto operands = x.as<ConcatenationExpression>().operands();
            return !operands.empty() && std::all_of(operands.begin(), operands.end(), [&](const Expression* o) { return enumBag(*o, element); });
        }
        if (x.kind == ExpressionKind::Replication) return enumBag(x.as<ReplicationExpression>().concat(), element);
        return false;
    }
    void recordEnumBag(const Type& to, const Expression& from) {
        if (!to.isPackedArray() || !from.type || !from.type->isIntegral()) return;
        const Expression& x = unwrapImplicit(from);
        // Only a piece-wise expression is judged here; any other conversion is the other proofs' (or has no site).
        if (x.kind != ExpressionKind::Concatenation && x.kind != ExpressionKind::Replication) return;
        const Type* t = &to.getCanonicalType();
        while (t->isPackedArray() && t->getArrayElementType()) t = &t->getArrayElementType()->getCanonicalType();
        if (!t->isEnum()) return;
        std::optional<std::string> proof;
        if (x.type->getBitWidth() == to.getBitWidth() && enumBag(x, *t))
            proof = "Value preserved: every piece of the operand is a value of enum '" + std::string(t->toString()) +
                    "', laid end to end over the " + std::to_string(to.getBitWidth()) + " bits of the array, so each element is an enumerator";
        record(Flavor::Sign, "packed-array-conv", x.sourceRange.start(), proof);
    }

    std::optional<std::string> conversionProof(const ConversionExpression& c) {
        if (auto s = valueProof(c)) return s;
        return concatProof(c);
    }

    // The concatenation's most significant operand is a constant with every bit zero.
    bool zeroExtends(const Expression& concat) const {
        const auto operands = concat.as<ConcatenationExpression>().operands();
        if (operands.size() < 2) return false;
        const auto top = l.evalConst(*operands[0]);
        if (!top.isInteger() || top.integer().hasUnknown()) return false;
        return top.integer().as<uint64_t>() == std::optional<uint64_t>(0);
    }
    // The converted value lies in the type it becomes. A cast layer of the target's own width only reinterprets the bits of what
    // is inside it, so `3'(i)` converted to 3 unsigned bits carries i itself.
    std::optional<std::string> valueProof(const ConversionExpression& c) {
        const auto to = typeRange(*c.type);
        if (!to) return std::nullopt;
        const Expression* p = &c.operand();
        while (p->kind == ExpressionKind::Conversion && p->type && p->type->getBitWidth() == c.type->getBitWidth() &&
               p->as<ConversionExpression>().operand().type && p->as<ConversionExpression>().operand().type->isIntegral())
            p = &p->as<ConversionExpression>().operand();
        // A concatenation is assembled by hand: its value says nothing of what the author meant (a sign extension one bit short
        // still has an unchanged value), so only the manual sign extension proof below applies to it, and a zero extension: a
        // concatenation whose first operand is the constant zero is the unsigned value of the rest, which the range of the whole proves
        // (`{1'b0, u8}` into a signed 9-bit holds 0..255). A first operand that is not a constant zero (`{x, u8}`) proves nothing.
        if (p->kind == ExpressionKind::Concatenation && !zeroExtends(*p)) return std::nullopt;
        if (p->kind == ExpressionKind::Replication) return std::nullopt;
        const auto in = ranges.of(*p);
        if (!in || !in->within(*to)) return std::nullopt;
        return "Value preserved: " + name(*p) + " lies in " + toString(*in) + ", which the " + c.type->toString() +
               " it is converted to holds unchanged";
    }

    // P3: {a[W-1], a} or {{K{a[W-1]}}, a}, with a a signed integral variable, a[W-1] its declared most significant bit,
    // converted to a signed type of width W+K.
    std::optional<std::string> concatProof(const ConversionExpression& c) {
        const Type& to = *c.type;
        const Expression& op = c.operand();
        if (!to.isSigned() || op.kind != ExpressionKind::Concatenation) return std::nullopt;
        const auto operands = op.as<ConcatenationExpression>().operands();
        if (operands.size() != 2) return std::nullopt;
        const Expression& base = unwrapImplicit(*operands[1]);
        if (base.kind != ExpressionKind::NamedValue) return std::nullopt;
        const ValueSymbol* sym = &base.as<NamedValueExpression>().symbol;
        const Type& at = sym->getType().getCanonicalType();
        if (!at.isIntegral() || !at.isSigned() || at.getBitWidth() == 0) return std::nullopt;
        const auto width = at.getBitWidth();

        const Expression* top = &unwrapImplicit(*operands[0]);
        int64_t copies = 1;
        if (top->kind == ExpressionKind::Replication) {
            const auto& rep = top->as<ReplicationExpression>();
            const auto count = constantInt(l, rep.count());
            if (!count || *count < 1) return std::nullopt;
            copies = *count;
            top = &unwrapImplicit(rep.concat());
            if (top->kind == ExpressionKind::Concatenation && top->as<ConcatenationExpression>().operands().size() == 1)
                top = &unwrapImplicit(*top->as<ConcatenationExpression>().operands()[0]);
        }
        if (top->kind != ExpressionKind::ElementSelect) return std::nullopt;
        const auto& sel = top->as<ElementSelectExpression>();
        const Expression& selected = unwrapImplicit(sel.value());
        if (selected.kind != ExpressionKind::NamedValue || &selected.as<NamedValueExpression>().symbol != sym) return std::nullopt;
        const auto index = constantInt(l, sel.selector());
        const int64_t msb = at.kind == SymbolKind::PackedArrayType ? at.getFixedRange().left : int64_t{width} - 1;
        if (!index || *index != msb) return std::nullopt;
        if (static_cast<int64_t>(to.getBitWidth()) != int64_t{width} + copies) return std::nullopt;

        const std::string name(sym->name);
        return "Value preserved: the operand extends '" + name + "' by repeating its sign bit '" + name + "[" + std::to_string(msb) +
               "]' " + std::to_string(copies) + (copies == 1 ? " time" : " times") + ", so the " + std::to_string(to.getBitWidth()) +
               "-bit signed result equals signed '" + name + "'";
    }

    Lint& l;
    ValueRanges ranges;
    int selfDetermined = 0;   // inside an index, a part-select bound or a shift count
};

// The lines of every definition no instance of the design uses. slang still elaborates it, with its parameters unresolved, and
// reports width and sign findings on constants it could not evaluate: they describe no hardware of this run (LNT-011 says the
// module is not linted).
struct Span {
    std::string file;
    uint32_t first, last;
};
std::vector<Span> unusedDefinitions(const Lint& lint) {
    std::vector<Span> spans;
    const auto& sm = lint.sm();
    for (const auto& member : lint.compilation().getRoot().members()) {
        const auto* inst = member.as_if<InstanceSymbol>();
        if (!inst || !inst->body.flags.has(InstanceFlags::Uninstantiated)) continue;
        const auto* syntax = inst->getDefinition().getSyntax();
        if (!syntax) continue;
        const auto range = syntax->sourceRange();
        const auto a = sm.getFullyExpandedLoc(range.start()), b = sm.getFullyExpandedLoc(range.end());
        if (!a.valid() || !b.valid() || a.buffer() != b.buffer()) continue;
        spans.push_back({sm.getFullPath(a.buffer()).generic_string(), static_cast<uint32_t>(sm.getLineNumber(a)),
                         static_cast<uint32_t>(sm.getLineNumber(b))});
    }
    return spans;
}

} // namespace

void demoteProvenFindings(RuleContext& ctx, const std::string& ruleId) {
    const bool sign = ruleId == "LNT-005";
    const auto candidate = [&](const Diagnostic& d) {
        if (d.ruleId != ruleId || d.origin != "slang" || d.severity != Severity::Warning) return false;
        if (sign) return d.upstreamCode == "sign-compare" || d.upstreamCode == "sign-conversion" || d.upstreamCode == "packed-array-conv" ||
                         d.upstreamCode == "arith-op-mismatch" || d.upstreamCode == "bitwise-op-mismatch";
        return d.upstreamCode == "arith-op-mismatch" || d.upstreamCode == "bitwise-op-mismatch" || d.upstreamCode == "width-expand" || d.upstreamCode == "width-trunc" ||
               d.upstreamCode == "port-width-trunc" || d.upstreamCode == "port-width-expand";
    };
    bool any = false;
    for (const auto& d : ctx.report.diagnostics()) any = any || candidate(d);
    if (!any) return;
    Lint lint(ctx, ruleId);
    ValueProofs proofs(lint);
    lint.visitHardware(proofs);
    for (const auto* package : lint.compilation().getPackages()) package->visit(proofs);   // functions and parameters of packages
    const auto unused = unusedDefinitions(lint);
    for (auto& d : ctx.report.mutableDiagnostics()) {
        if (!candidate(d)) continue;
        const bool inUnused = std::any_of(unused.begin(), unused.end(), [&](const Span& s) {
            return s.file == d.location.file && d.location.line >= s.first && d.location.line <= s.last;
        });
        if (inUnused) {
            // Advisory, never dropped: nothing here is evaluated, so nothing is proven either way.
            static const std::string sentence = "This definition is not instantiated in this design, so slang elaborated it with unresolved "
                                                "parameters and the finding is not evaluated; lint it under a top that uses it";
            d.severity = Severity::Info;
            d.message += ". " + sentence + ".";
            d.evidence.push_back({"definition_not_instantiated", sentence, {}});
            d.nextStep = "Lint the definition under a top that instantiates it.";
            d.applyDefaultDisposition();   // the disposition follows the new severity
            continue;
        }
        const auto it = proofs.sites.find(std::string(sign ? "sign:" : "width:") + d.upstreamCode + "@" + keyOf(d.location));
        if (it == proofs.sites.end() || !it->second.proven || it->second.sentence.empty()) continue;
        // Kept as INFO, never dropped: the original message stays and the proof is appended and recorded as evidence.
        d.severity = Severity::Info;
        d.confidence = Confidence::Established;
        d.message += ". " + it->second.sentence + ".";
        d.evidence.push_back({"value_preserved", it->second.sentence, {}});
        d.nextStep = "No action: the value is proven to be preserved.";
        d.applyDefaultDisposition();   // the disposition follows the new severity
    }
}

} // namespace sverity
