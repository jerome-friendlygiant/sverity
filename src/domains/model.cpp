#include "domains/model.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>

#include "analysis/flow.h"
#include "analysis/temps.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/expressions/AssertionExpr.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"

namespace sverity::model {

using namespace slang;
using namespace slang::ast;

std::optional<bool> elaboratedCondition(const Expression& expression, EvalContext& context) {
    struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
        bool found = false;
        void handle(const CallExpression&) { found = true; }
    } calls;
    expression.visit(calls);
    if (calls.found) return std::nullopt;
    const auto value = expression.eval(context);
    if (!value.isInteger() || value.hasUnknown()) return std::nullopt;
    return value.isTrue();
}

namespace {

constexpr uint32_t kMaxBits = 16384;         // wider symbols are collapsed to one node (recorded fallback); exact bit moves stay linear
constexpr uint32_t kMaxPackedBits = 131072;  // ... except a packed vector or struct, which is sliced by constant selects and stays per bit

// A packed (integral) value is assembled and taken apart by constant part-selects, so per-bit tracking costs one node per bit used.
// An unpacked array is a memory, indexed at run time: past kMaxBits it is one node.
bool tooWide(const Type& type, uint64_t width) { return width > (type.isIntegral() ? kMaxPackedBits : kMaxBits); }
constexpr uint64_t kMaxEdgesPerAssign = 1u << 22;
constexpr uint64_t kHubEdgeThreshold = 1u << 10;
constexpr size_t kCompactEdgesFrom = size_t{1} << 24;
constexpr size_t kHubReadSet = 32;

struct Path {
    const ValueSymbol* sym = nullptr;
    uint64_t lo = 0, hi = 0;
    bool operator<(const Path& o) const { return std::tie(sym, lo, hi) < std::tie(o.sym, o.lo, o.hi); }
    bool operator==(const Path& o) const { return sym == o.sym && lo == o.lo && hi == o.hi; }
};

uint64_t widthOf(const ValueSymbol& s) {
    const Type& t = s.getType();
    if (t.isFixedSize()) return t.getBitstreamWidth();
    return 0;
}

bool isGraphSymbol(const ValueSymbol& s) {
    return s.kind == SymbolKind::Net || s.kind == SymbolKind::Variable;
}

const InstanceBodySymbol* bodyOf(const Symbol& s) {
    const Scope* sc = s.getParentScope();
    while (sc) {
        const Symbol& ss = sc->asSymbol();
        if (ss.kind == SymbolKind::InstanceBody) return &ss.as<InstanceBodySymbol>();
        sc = ss.getParentScope();
    }
    return nullptr;
}

/// True when the symbol is declared at module/generate level (a potential register), not local to a procedure.
bool isModuleLevel(const Symbol& s) {
    const Scope* sc = s.getParentScope();
    if (!sc) return false;
    SymbolKind k = sc->asSymbol().kind;
    return k == SymbolKind::InstanceBody || k == SymbolKind::GenerateBlock;
}

bool isPackageState(const Symbol& s) {
    const auto* scope = s.getParentScope();
    return scope && scope->asSymbol().kind == SymbolKind::Package;
}

SourceLocation toLoc(const SourceManager& sm, slang::SourceLocation l) {
    SourceLocation out;
    if (!l.valid()) return out;
    try {
        out.file = sm.getFullPath(l.buffer()).generic_string();
        out.line = static_cast<uint32_t>(sm.getLineNumber(l));
        out.column = static_cast<uint32_t>(sm.getColumnNumber(l));
    } catch (...) { out = {}; }
    return out;
}

struct NodeKey {
    const InstanceSymbol* inst; const ValueSymbol* sym; uint32_t bit;
    bool operator==(const NodeKey&) const = default;
};
struct NodeKeyHash {
    size_t operator()(const NodeKey& k) const {
        size_t h = std::hash<const void*>()(k.inst);
        h = h * 1000003u ^ std::hash<const void*>()(k.sym);
        return h * 1000003u ^ k.bit;
    }
};

// ---- operator classification (Edge::op / Edge::inv)
struct OpInfo {
    Op op = Op::None;
    uint8_t inv = 0;
    Branch br = Branch::None;
};
using OpMap = std::map<const ValueSymbol*, OpInfo>;

void noteOp(OpMap& out, const ValueSymbol* sym, Op op, uint8_t inv, Branch br = Branch::None) {
    auto [it, fresh] = out.emplace(sym, OpInfo{op, inv, br});
    if (fresh) return;
    if (it->second.op != op) it->second.op = Op::Other;
    if (it->second.inv != inv) it->second.inv = 2;
    if (it->second.br != br) it->second.br = Branch::None;
}

uint8_t flipInv(uint8_t i) { return i == 2 ? 2 : static_cast<uint8_t>(i ^ 1); }

struct AnyRefs : ASTVisitor<AnyRefs, VisitFlags::AllGood> {
    OpMap& out;
    explicit AnyRefs(OpMap& o) : out(o) {}
    void handle(const NamedValueExpression& e) { noteOp(out, &e.symbol, Op::Other, 2); }
    void handle(const HierarchicalValueExpression& e) { noteOp(out, &e.symbol, Op::Other, 2); }
};

/// Records, for every variable referenced in `e`, the innermost operator around it and whether it arrives inverted.
void classify(const Expression& e, Op parent, uint8_t inv, OpMap& out, Op anc = Op::None) {
    switch (e.kind) {
        case ExpressionKind::NamedValue: noteOp(out, &e.as<NamedValueExpression>().symbol, anc == Op::Other ? Op::Other : parent, anc == Op::Other ? 2 : inv); return;
        case ExpressionKind::HierarchicalValue: noteOp(out, &e.as<HierarchicalValueExpression>().symbol, anc == Op::Other ? Op::Other : parent, anc == Op::Other ? 2 : inv); return;
        case ExpressionKind::ElementSelect: {
            auto& x = e.as<ElementSelectExpression>();
            classify(x.value(), parent, inv, out, anc);
            classify(x.selector(), Op::Select, 2, out, anc);
            return;
        }
        case ExpressionKind::RangeSelect: {
            auto& x = e.as<RangeSelectExpression>();
            classify(x.value(), parent, inv, out, anc);
            classify(x.left(), Op::Select, 2, out, anc);
            classify(x.right(), Op::Select, 2, out, anc);
            return;
        }
        case ExpressionKind::MemberAccess: classify(e.as<MemberAccessExpression>().value(), parent, inv, out, anc); return;
        case ExpressionKind::Conversion: classify(e.as<ConversionExpression>().operand(), parent, inv, out, anc); return;
        case ExpressionKind::Concatenation:
            for (auto* op : e.as<ConcatenationExpression>().operands()) classify(*op, parent, inv, out, anc);
            return;
        case ExpressionKind::Replication: classify(e.as<ReplicationExpression>().concat(), parent, inv, out, anc); return;
        case ExpressionKind::UnaryOp: {
            auto& u = e.as<UnaryExpression>();
            switch (u.op) {
                case UnaryOperator::BitwiseNot:
                case UnaryOperator::LogicalNot: classify(u.operand(), Op::Not, flipInv(inv), out, anc); return;
                case UnaryOperator::BitwiseAnd: classify(u.operand(), Op::And, inv, out, anc); return;
                case UnaryOperator::BitwiseNand: classify(u.operand(), Op::And, flipInv(inv), out, anc); return;
                case UnaryOperator::BitwiseOr: classify(u.operand(), Op::Or, inv, out, anc); return;
                case UnaryOperator::BitwiseNor: classify(u.operand(), Op::Or, flipInv(inv), out, anc); return;
                case UnaryOperator::BitwiseXor: classify(u.operand(), Op::Xor, inv, out, anc); return;
                case UnaryOperator::BitwiseXnor: classify(u.operand(), Op::Xor, flipInv(inv), out, anc); return;
                case UnaryOperator::Plus: classify(u.operand(), parent, inv, out, anc); return;
                default: classify(u.operand(), Op::Arith, 2, out, anc); return;
            }
        }
        case ExpressionKind::BinaryOp: {
            auto& b = e.as<BinaryExpression>();
            Op op = Op::Other;
            uint8_t i = inv;
            switch (b.op) {
                case BinaryOperator::BinaryAnd: case BinaryOperator::LogicalAnd: op = Op::And; break;
                case BinaryOperator::BinaryOr: case BinaryOperator::LogicalOr: op = Op::Or; break;
                case BinaryOperator::BinaryXor: op = Op::Xor; break;
                case BinaryOperator::BinaryXnor: op = Op::Xor; i = flipInv(inv); break;
                case BinaryOperator::Add: case BinaryOperator::Subtract: case BinaryOperator::Multiply:
                case BinaryOperator::Divide: case BinaryOperator::Mod: case BinaryOperator::Power: op = Op::Arith; i = 2; break;
                case BinaryOperator::LogicalShiftLeft: case BinaryOperator::LogicalShiftRight:
                case BinaryOperator::ArithmeticShiftLeft: case BinaryOperator::ArithmeticShiftRight: op = Op::Shift; i = 2; break;
                case BinaryOperator::Equality: case BinaryOperator::Inequality: case BinaryOperator::CaseEquality:
                case BinaryOperator::CaseInequality: case BinaryOperator::WildcardEquality: case BinaryOperator::WildcardInequality:
                case BinaryOperator::GreaterThanEqual: case BinaryOperator::GreaterThan: case BinaryOperator::LessThanEqual:
                case BinaryOperator::LessThan: op = Op::Compare; i = 2; break;
                default: op = Op::Other; i = 2; break;
            }
            Op nanc = anc;
            if (op == Op::And || op == Op::Or) nanc = (anc == Op::None || anc == op) ? op : Op::Other;
            classify(b.left(), op, i, out, nanc);
            classify(b.right(), op, i, out, nanc);
            return;
        }
        case ExpressionKind::ConditionalOp: {
            auto& c = e.as<ConditionalExpression>();
            for (auto& cond : c.conditions) classify(*cond.expr, Op::MuxSel, 2, out, anc);
            classify(c.left(), Op::Mux, inv, out, anc);
            classify(c.right(), Op::Mux, inv, out, anc);
            return;
        }
        default: {
            AnyRefs v(out);
            e.visit(v);
            return;
        }
    }
}

struct BitRef {
    const ValueSymbol* sym = nullptr;   // nullptr = constant bit
    uint32_t bit = 0;
    char value = 0;                     // constant bit: '0' '1' 'x' 'z', 0 when not known
};

struct Assign {
    const AssignmentExpression* asg = nullptr;
    std::vector<std::tuple<SourceLocation, std::vector<Path>, std::string, bool>> calledConditions;
    std::vector<std::pair<SourceLocation, std::vector<Path>>> conditions;
    std::vector<Path> targets;
    std::vector<Path> dataReads;   // RHS (and compound-assignment target) reads
    std::vector<Path> ctrlReads;   // control-flow conditions + selector reads
    OpMap dataOps, ctrlOps;        // operator context of each referenced variable (RHS / enclosing conditions)
    bool nonBlocking = false;
    bool called = false;
    bool unattributedWrite = false;
    bool afterPossibleExit = false;
    bool pairsValid = false;       // dataReads are fully described by exact bit pairs below
    bool flipPairsValid = false;   // the same pairs, each source bit reaching its target inverted (`~x`)
    std::vector<std::pair<BitRef, BitRef>> pairs;   // (target bit, source bit or constant)
    bool approximate = false;      // a computed index or a user call: its bits are not known one by one
    std::vector<std::pair<BitRef, BitRef>> preciseBits;   // approximate, bitwise chain: (target bit, source bit) of operands that keep each bit in place
    std::vector<Path> preciseReads;   // approximate, one-bit result: the reads that are plain static selects, each a certain dependency
    bool bitDepsValid = false;     // not exact, but dataReads are described bit by bit below
    std::vector<std::pair<BitRef, std::vector<Path>>> bitDeps;   // (target bit, the reads that can influence that bit)
    std::vector<char> bitDepExact; // parallel to bitDeps: the bit is a plain move from one source bit
    std::vector<std::pair<BitRef, char>> bitMoves;   // parallel to bitDeps: the one source bit and 1 (plain move) or 2 (inverted move); sym null = not a move
    SourceLocation loc;
    BoolFactPtr booleanValue;
    std::vector<GuardCondition> booleanGuards;
    std::vector<std::pair<NodeId, std::vector<CombQualifierArm>>> qualifierArms;
    std::vector<uint32_t> uncertainCalls;
};

/// Finds a statement that may leave the block it sits in: a `return`, a `disable`, or a `break` / `continue` that is not inside a
/// loop of the visited statement itself.
struct JumpFinder : ASTVisitor<JumpFinder, VisitFlags::AllGood> {
    int loops = 0;
    bool found = false;
    void handle(const ReturnStatement&) { found = true; }
    void handle(const DisableStatement&) { found = true; }
    void handle(const DisableForkStatement&) { found = true; }
    void handle(const BreakStatement&) { if (!loops) found = true; }
    void handle(const ContinueStatement&) { if (!loops) found = true; }
    template<class T> void handle(const T& node) {
        constexpr bool loop = std::is_same_v<T, ForLoopStatement> || std::is_same_v<T, WhileLoopStatement> ||
                              std::is_same_v<T, DoWhileLoopStatement> || std::is_same_v<T, RepeatLoopStatement> ||
                              std::is_same_v<T, ForeachLoopStatement> || std::is_same_v<T, ForeverLoopStatement>;
        if constexpr (loop) ++loops;
        visitDefault(node);
        if constexpr (loop) --loops;
    }
};

/// Whitelist of synthesizable statements and pure/non-suspending calls permitted
/// inside a procedure whose registers may be proven constant-stuck (or whose
/// combinational aliases may skip execution barriers).
struct StuckBodyChecker : ASTVisitor<StuckBodyChecker, VisitFlags::AllGood> {
    const Statement* outerTimed = nullptr;
    bool unsafe = false;
    unsigned callDepth = 0;
    size_t callExpansions = 0;
    std::set<const SubroutineSymbol*> visiting;
    static bool isPureSystemCall(const CallExpression& c) {
        if (!c.isSystemCall()) return false;
        const auto& name = std::get<CallExpression::SystemCallInfo>(c.subroutine).subroutine->name;
        static const std::set<std::string_view> pure = {
            "$clog2", "$signed", "$unsigned", "$countones", "$onehot", "$onehot0", "$isunknown",
            "$bits", "$typename", "$size", "$high", "$low", "$left", "$right", "$increment", "$dimensions"
        };
        return pure.count(name) != 0;
    }
    static bool isNonEmptyStmt(const Statement* s) {
        return s && s->kind != StatementKind::Empty;
    }
    void handle(const NewClassExpression&) { unsafe = true; }
    void handle(const NewArrayExpression&) { unsafe = true; }
    void handle(const NewCovergroupExpression&) { unsafe = true; }
    void handle(const CopyClassExpression&) { unsafe = true; }
    void handle(const ClockingEventExpression&) { unsafe = true; }
    void handle(const AssertionInstanceExpression&) { unsafe = true; }
    void handle(const AssignmentExpression& a) {
        if (a.timingControl) unsafe = true;
        visitDefault(a);
    }
    void handle(const CallExpression& c) {
        if (c.isSystemCall()) {
            if (!isPureSystemCall(c)) unsafe = true;
        } else if (c.thisClass()) {
            unsafe = true;
        } else {
            const auto* sub = std::get<const SubroutineSymbol*>(c.subroutine);
            bool ok = sub && sub->subroutineKind == SubroutineKind::Function &&
                      !sub->isVirtual() &&
                      !sub->flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) &&
                      !sub->getBody().bad() &&
                      callDepth < 32 && callExpansions < 256 &&
                      !visiting.count(sub);
            if (ok) {
                for (const auto* arg : sub->getArguments()) {
                    if (arg->direction != ArgumentDirection::In) { ok = false; break; }
                }
            }
            if (!ok) {
                unsafe = true;
            } else {
                ++callExpansions;
                visiting.insert(sub);
                ++callDepth;
                sub->getBody().visit(*this);
                --callDepth;
                visiting.erase(sub);
            }
        }
        visitDefault(c);
    }
    template <typename T>
        requires(std::is_base_of_v<Statement, T> && !std::is_same_v<T, Statement>)
    void handle(const T& s) {
        switch (s.kind) {
            case StatementKind::Empty:
            case StatementKind::List:
            case StatementKind::ExpressionStatement:
            case StatementKind::Conditional:
            case StatementKind::Case:
            case StatementKind::WhileLoop:
            case StatementKind::DoWhileLoop:
            case StatementKind::RepeatLoop:
            case StatementKind::ForeachLoop:
            case StatementKind::Return:
            case StatementKind::Break:
            case StatementKind::Continue:
                break;
            case StatementKind::Disable:
                if (callDepth > 0) unsafe = true;
                break;
            case StatementKind::VariableDeclaration:
                if constexpr (std::is_same_v<T, VariableDeclStatement>) {
                    if (const auto* init = s.symbol.getInitializer()) init->visit(*this);
                }
                break;
            case StatementKind::ForLoop:
                if constexpr (std::is_same_v<T, ForLoopStatement>) {
                    for (const auto* var : s.loopVars)
                        if (const auto* init = var->getInitializer()) init->visit(*this);
                }
                break;
            case StatementKind::Block:
                if constexpr (std::is_same_v<T, BlockStatement>) {
                    if (s.blockKind != StatementBlockKind::Sequential) unsafe = true;
                }
                break;
            case StatementKind::Timed:
                if (&s != outerTimed) unsafe = true;
                break;
            case StatementKind::ImmediateAssertion:
                if constexpr (std::is_same_v<T, ImmediateAssertionStatement>) {
                    if (isNonEmptyStmt(s.ifTrue) || isNonEmptyStmt(s.ifFalse)) unsafe = true;
                }
                break;
            case StatementKind::ConcurrentAssertion:
                if constexpr (std::is_same_v<T, ConcurrentAssertionStatement>) {
                    if (s.assertionKind == AssertionKind::Expect ||
                        isNonEmptyStmt(s.ifTrue) || isNonEmptyStmt(s.ifFalse))
                        unsafe = true;
                }
                break;
            default:
                unsafe = true;
                break;
        }
        visitDefault(s);
    }
};

class Builder {
public:
    Builder(Design& d, Model& m) : design(d), m(m), sm(d.compilation->getSourceManager()) {}

    /// Which processes (nullptr = continuous assignments, port connections, other contexts) read each variable.
    struct ReaderCollector : ASTVisitor<ReaderCollector, VisitFlags::AllGood> {
        std::unordered_map<const ValueSymbol*, std::set<const Symbol*>>& readers;
        const Symbol* owner = nullptr;
        explicit ReaderCollector(std::unordered_map<const ValueSymbol*, std::set<const Symbol*>>& r) : readers(r) {}
        void handle(const ProceduralBlockSymbol& p) {
            auto* saved = owner;
            owner = &p;
            visitDefault(p);
            owner = saved;
        }
        void handle(const NamedValueExpression& e) {
            if (auto* s = e.getSymbolReference(); s && s->isValue()) readers[&s->as<ValueSymbol>()].insert(owner);
        }
        void handle(const AssignmentExpression& a) {
            if (a.isCompound() || a.left().kind != ExpressionKind::NamedValue) a.left().visit(*this);   // plain whole-variable writes are not reads
            a.right().visit(*this);
        }
    };
    std::unordered_map<const ValueSymbol*, std::set<const Symbol*>> readers_;
    bool readByOther(const ValueSymbol* s, const Symbol* self) const {
        auto it = readers_.find(s);
        if (it == readers_.end()) return false;
        for (auto* o : it->second) if (o != self) return true;
        return false;
    }

    void run() {
        auto& root = design.compilation->getRoot();
        // A module-level variable that every procedure writes before it reads is scratch space, not state: no dependency through it
        // is a loop, and it feeds nothing a cone cannot already see.
        m.procedureTemps = analysis::procedureTemporaries(*design.compilation);
        m.loopVariables.insert(m.procedureTemps.begin(), m.procedureTemps.end());
        { ReaderCollector rc(readers_); root.visit(rc); }
        for (auto* top : root.topInstances) collectInstances(*top);
        for (auto* top : root.topInstances) {
            top_.insert(top);
            // A top that says it is a black box has no hardware body to walk: it is all unmodelled.
            if (attributedBlackBox(top->getDefinition(), *top->getParentScope())) {
                BlackBoxInst bb;
                bb.module = std::string(top->getDefinition().name);
                bb.hierarchy = top->getHierarchicalPath();
                bb.loc = toLoc(*sm, top->location);
                bb.attributed = true;
                m.blackBoxes.push_back(std::move(bb));
                m.numInstances++;
                continue;
            }
            walkInstance(*top);
        }
        // mark top-level input ports
        for (auto* top : root.topInstances) markTopInputs(*top);
        applyFlags();
        for (NodeId n = 0; n < m.nodes.size(); ++n) {
            auto it = design.caseConstants.find(m.nodes[n].sym);
            if (it != design.caseConstants.end()) m.modeConstants[n] = it->second ? '1' : '0';
            if (auto bits = design.caseBits.find(m.nodes[n].sym); bits != design.caseBits.end() && bits->second[m.nodes[n].bit] >= 0)
                m.modeConstants[n] = bits->second[m.nodes[n].bit] ? '1' : '0';
        }
        m.finish();
        std::sort(unknownWriteCalls.begin(), unknownWriteCalls.end());
        unknownWriteCalls.erase(std::unique(unknownWriteCalls.begin(), unknownWriteCalls.end()), unknownWriteCalls.end());
        for (auto issue : unknownWriteCalls)
            for (const auto& flop : m.flops) m.uncertainCallDrivers[flop.q].push_back(issue);
        for (auto& [node, issues] : m.uncertainCallDrivers) {
            std::sort(issues.begin(), issues.end()); issues.erase(std::unique(issues.begin(), issues.end()), issues.end());
        }
    }

private:
    Design& design;
    Model& m;
    const SourceManager* sm;
    std::unordered_map<NodeKey, NodeId, NodeKeyHash> index;
    std::unordered_map<const InstanceBodySymbol*, std::vector<const InstanceSymbol*>> bodyInstances;
    std::set<const InstanceSymbol*> top_;
    std::set<const ValueSymbol*> wideReported;
    std::vector<std::pair<NodeId, uint8_t>> pendingFlags;

    void fallback(const std::string& cat, const std::string& detail, slang::SourceLocation l) {
        m.fallbacks.push_back({cat, detail, toLoc(*sm, l)});
    }

    // ---------------------------------------------------------------- nodes
    uint32_t collapse(const ValueSymbol& s, uint64_t w) {
        if (tooWide(s.getType(), w)) {
            if (wideReported.insert(&s).second) {
                fallback("wide-symbol", "'" + std::string(s.name) + "' is " + std::to_string(w) +
                         " bits wide; tracked as a single node (bit precision lost)", s.location);
            }
            return 1;   // collapsed
        }
        return 0;
    }

    NodeId node(const InstanceSymbol* inst, const ValueSymbol* sym, uint32_t bit) {
        NodeKey k{inst, sym, bit};
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        NodeId id = static_cast<NodeId>(m.nodes.size());
        m.nodes.push_back({inst, sym, bit});
        index.emplace(k, id);
        return id;
    }

    /// All node ids for the bit range of a path, resolving the owning instance of the symbol.
    std::vector<NodeId> nodesFor(const InstanceSymbol* ctx, const Path& p) {
        const InstanceSymbol* inst = ownerOf(*p.sym, ctx);
        uint64_t w = widthOf(*p.sym);
        std::vector<NodeId> v;
        if (w == 0 || collapse(*p.sym, w)) { v.push_back(node(inst, p.sym, 0)); return v; }
        uint64_t lo = std::min<uint64_t>(p.lo, w - 1), hi = std::min<uint64_t>(p.hi, w - 1);
        if (lo > hi) std::swap(lo, hi);
        for (uint64_t b = lo; b <= hi; b++) v.push_back(node(inst, p.sym, static_cast<uint32_t>(b)));
        return v;
    }

    const InstanceSymbol* ownerOf(const ValueSymbol& s, const InstanceSymbol* ctx) {
        // Package state is one design-wide object, not a copy per module
        // instance. Keep it localizable so an opaque call that writes a
        // package variable does not poison every register in the design.
        for (auto scope = s.getParentScope(); scope; scope = scope->asSymbol().getParentScope())
            if (scope->asSymbol().kind == SymbolKind::Package) return nullptr;
        const InstanceBodySymbol* b = bodyOf(s);
        if (!b || !ctx || b == &ctx->body) return ctx;
        auto it = bodyInstances.find(b);
        if (it != bodyInstances.end() && it->second.size() == 1) return it->second[0];
        fallback("hierarchical-reference",
                 "reference to '" + std::string(s.name) + "' outside the current instance has an ambiguous owner; "
                 "attributed to the referencing instance", s.location);
        return ctx;
    }

    // ---------------------------------------------------------------- paths
    static bool hasAnyCall(const Expression& expr) {
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression&) { found = true; }
        } calls;
        expr.visit(calls);
        return calls.found;
    }
    static const Expression& stripWidthConversions(const Expression& expr) {
        const Expression* x = &expr;
        while (x->kind == ExpressionKind::Conversion) {
            const auto& c = x->as<ConversionExpression>();
            if (!c.type || !c.operand().type ||
                c.type->getBitstreamWidth() != c.operand().type->getBitstreamWidth()) break;
            x = &c.operand();
        }
        return *x;
    }
    static bool samePureExpr(const Expression& rawA, const Expression& rawB) {
        const Expression& a = stripWidthConversions(rawA);
        const Expression& b = stripWidthConversions(rawB);
        if (a.kind != b.kind) return false;
        switch (a.kind) {
            case ExpressionKind::NamedValue:
            case ExpressionKind::HierarchicalValue:
                return a.getSymbolReference() && a.getSymbolReference() == b.getSymbolReference();
            case ExpressionKind::IntegerLiteral:
                return exactlyEqual(a.as<IntegerLiteral>().getValue(), b.as<IntegerLiteral>().getValue());
            case ExpressionKind::ElementSelect: {
                const auto& sa = a.as<ElementSelectExpression>();
                const auto& sb = b.as<ElementSelectExpression>();
                if (!samePureExpr(sa.value(), sb.value())) return false;
                const auto* ca = sa.selector().getConstant();
                const auto* cb = sb.selector().getConstant();
                return ca && cb && ca->isInteger() && cb->isInteger() &&
                       !ca->integer().hasUnknown() && !cb->integer().hasUnknown() &&
                       exactlyEqual(ca->integer(), cb->integer());
            }
            case ExpressionKind::RangeSelect: {
                const auto& ra = a.as<RangeSelectExpression>();
                const auto& rb = b.as<RangeSelectExpression>();
                if (ra.getSelectionKind() != rb.getSelectionKind() || !samePureExpr(ra.value(), rb.value())) return false;
                const auto* la = ra.left().getConstant();
                const auto* lb = rb.left().getConstant();
                const auto* rca = ra.right().getConstant();
                const auto* rcb = rb.right().getConstant();
                return la && lb && rca && rcb && la->isInteger() && lb->isInteger() &&
                       rca->isInteger() && rcb->isInteger() &&
                       !la->integer().hasUnknown() && !lb->integer().hasUnknown() &&
                       !rca->integer().hasUnknown() && !rcb->integer().hasUnknown() &&
                       exactlyEqual(la->integer(), lb->integer()) &&
                       exactlyEqual(rca->integer(), rcb->integer());
            }
            case ExpressionKind::MemberAccess: {
                const auto& ma = a.as<MemberAccessExpression>();
                const auto& mb = b.as<MemberAccessExpression>();
                return &ma.member == &mb.member && samePureExpr(ma.value(), mb.value());
            }
            case ExpressionKind::UnaryOp: {
                const auto& ua = a.as<UnaryExpression>();
                const auto& ub = b.as<UnaryExpression>();
                return ua.op == ub.op && samePureExpr(ua.operand(), ub.operand());
            }
            case ExpressionKind::BinaryOp: {
                const auto& ba = a.as<BinaryExpression>();
                const auto& bb = b.as<BinaryExpression>();
                if (ba.op != bb.op) return false;
                if (samePureExpr(ba.left(), bb.left()) && samePureExpr(ba.right(), bb.right())) return true;
                switch (ba.op) {
                    case BinaryOperator::BinaryXor:
                    case BinaryOperator::BinaryXnor:
                    case BinaryOperator::BinaryAnd:
                    case BinaryOperator::BinaryOr:
                    case BinaryOperator::LogicalAnd:
                    case BinaryOperator::LogicalOr:
                    case BinaryOperator::Equality:
                    case BinaryOperator::Inequality:
                    case BinaryOperator::CaseEquality:
                    case BinaryOperator::CaseInequality:
                        return samePureExpr(ba.left(), bb.right()) && samePureExpr(ba.right(), bb.left());
                    default:
                        return false;
                }
            }
            default:
                return false;
        }
    }
public:
    enum class ConstFold { None, Zero, AllOnes, OneBitTrue, OtherConst };
    static ConstFold foldConstantExpr(const Expression& expr, EvalContext& ec, bool checkedCalls = false,
                                      const Model* model = nullptr,
                                      const std::unordered_map<const ValueSymbol*, std::vector<NodeId>>* symNodes = nullptr) {
        if (!checkedCalls && hasAnyCall(expr)) return ConstFold::None;
        if (auto cv = expr.eval(ec); cv.isInteger() && !cv.integer().hasUnknown()) {
            if (cv.integer() == 0) return ConstFold::Zero;
            if (cv.integer().countOnes() == cv.integer().getBitWidth()) return ConstFold::AllOnes;
            if (cv.isTrue()) return ConstFold::OneBitTrue;
            return ConstFold::OtherConst;
        }
        if (model && symNodes) {
            if (expr.kind == ExpressionKind::NamedValue || expr.kind == ExpressionKind::HierarchicalValue) {
                const auto* ref = expr.getSymbolReference();
                const auto* vs = ref ? ref->as_if<ValueSymbol>() : nullptr;
                if (vs) {
                    if (auto it = symNodes->find(vs); it != symNodes->end() && !it->second.empty()) {
                        bool allZero = true, allOne = true;
                        for (NodeId n : it->second) {
                            const char cv = model->constantValue(n);
                            if (cv != '0') allZero = false;
                            if (cv != '1') allOne = false;
                        }
                        if (allZero) return ConstFold::Zero;
                        if (allOne) return ConstFold::AllOnes;
                    }
                }
            } else if (expr.kind == ExpressionKind::ElementSelect) {
                const auto& sel = expr.as<ElementSelectExpression>();
                const auto* ref = stripWidthConversions(sel.value()).getSymbolReference();
                const auto* vs = ref ? ref->as_if<ValueSymbol>() : nullptr;
                if (vs) {
                    auto idx = sel.selector().eval(ec);
                    if (idx.isInteger() && !idx.integer().hasUnknown()) {
                        if (auto iv = idx.integer().as<int64_t>(); iv && *iv >= 0) {
                            if (auto it = symNodes->find(vs); it != symNodes->end()) {
                                for (NodeId n : it->second) {
                                    if (n < model->nodes.size() && model->nodes[n].bit == static_cast<uint32_t>(*iv)) {
                                        const char cv = model->constantValue(n);
                                        if (cv == '0') return ConstFold::Zero;
                                        if (cv == '1') return ConstFold::AllOnes;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (expr.kind == ExpressionKind::Conversion) {
            const auto& c = expr.as<ConversionExpression>();
            const auto inner = foldConstantExpr(c.operand(), ec, true, model, symNodes);
            if (inner == ConstFold::None) return ConstFold::None;
            if (c.type && c.operand().type) {
                const uint64_t toW = c.type->getBitstreamWidth();
                const uint64_t fromW = c.operand().type->getBitstreamWidth();
                if (toW == fromW) return inner;
                if (toW > fromW && inner == ConstFold::Zero && !c.operand().type->isSigned())
                    return ConstFold::Zero;
            }
            return ConstFold::None;
        }
        if (expr.kind == ExpressionKind::UnaryOp) {
            const auto& u = expr.as<UnaryExpression>();
            const auto inner = foldConstantExpr(u.operand(), ec, true, model, symNodes);
            if (inner == ConstFold::None) return ConstFold::None;
            if (u.op == UnaryOperator::LogicalNot)
                return inner == ConstFold::Zero ? ConstFold::OneBitTrue : ConstFold::Zero;
            if (u.op == UnaryOperator::BitwiseNot)
                return inner == ConstFold::Zero ? ConstFold::AllOnes :
                       inner == ConstFold::AllOnes ? ConstFold::Zero : ConstFold::OtherConst;
            return ConstFold::None;
        }
        if (expr.kind == ExpressionKind::BinaryOp) {
            const auto& b = expr.as<BinaryExpression>();
            const auto lf = foldConstantExpr(b.left(), ec, true, model, symNodes);
            const auto rf = foldConstantExpr(b.right(), ec, true, model, symNodes);
            if (b.op == BinaryOperator::LogicalAnd) {
                if (lf == ConstFold::Zero || rf == ConstFold::Zero) return ConstFold::Zero;
                if (lf != ConstFold::None && rf != ConstFold::None) return ConstFold::OneBitTrue;
            }
            if (b.op == BinaryOperator::LogicalOr) {
                if ((lf != ConstFold::None && lf != ConstFold::Zero) ||
                    (rf != ConstFold::None && rf != ConstFold::Zero)) return ConstFold::OneBitTrue;
                if (lf == ConstFold::Zero && rf == ConstFold::Zero) return ConstFold::Zero;
            }
            if (b.op == BinaryOperator::BinaryAnd && (lf == ConstFold::Zero || rf == ConstFold::Zero))
                return ConstFold::Zero;
            if (b.op == BinaryOperator::BinaryOr && (lf == ConstFold::AllOnes || rf == ConstFold::AllOnes))
                return ConstFold::AllOnes;
            if (samePureExpr(b.left(), b.right())) {
                switch (b.op) {
                    case BinaryOperator::BinaryXor:
                    case BinaryOperator::Subtract:
                    case BinaryOperator::Inequality:
                    case BinaryOperator::CaseInequality:
                    case BinaryOperator::LessThan:
                    case BinaryOperator::GreaterThan:
                        return ConstFold::Zero;
                    case BinaryOperator::BinaryXnor:
                        return ConstFold::AllOnes;
                    case BinaryOperator::Equality:
                    case BinaryOperator::CaseEquality:
                    case BinaryOperator::LessThanEqual:
                    case BinaryOperator::GreaterThanEqual:
                        return ConstFold::OneBitTrue;
                    default: break;
                }
            }
            auto complementary = [&](const Expression& lhs, const Expression& rhs) {
                const Expression& x = stripWidthConversions(lhs);
                if (x.kind != ExpressionKind::UnaryOp) return false;
                const auto& u = x.as<UnaryExpression>();
                if (u.op != UnaryOperator::BitwiseNot && u.op != UnaryOperator::LogicalNot) return false;
                if (u.op == UnaryOperator::LogicalNot &&
                    (!rhs.type || rhs.type->getBitstreamWidth() != 1 ||
                     !u.operand().type || u.operand().type->getBitstreamWidth() != 1))
                    return false;
                return samePureExpr(u.operand(), rhs);
            };
            if (complementary(b.left(), b.right()) || complementary(b.right(), b.left())) {
                switch (b.op) {
                    case BinaryOperator::BinaryAnd:
                    case BinaryOperator::LogicalAnd:
                    case BinaryOperator::BinaryXnor:
                        return ConstFold::Zero;
                    case BinaryOperator::BinaryOr:
                    case BinaryOperator::BinaryXor:
                        return ConstFold::AllOnes;
                    case BinaryOperator::LogicalOr:
                        return ConstFold::OneBitTrue;
                    default: break;
                }
            }
        }
        return ConstFold::None;
    }
private:

    // The reads of an expression that can reach its value: the dead arm of a `?:`, and the operand of `&&` / `||` that a constant
    // left operand short-circuits, are not read (a bound loop variable makes `i > 0 ? fb[i-1] : 0` constant per iteration).
    struct LivePaths : ASTVisitor<LivePaths, VisitFlags::AllGood> {
        EvalContext& ec;
        function_ref<void(const ValuePath&)> cb;
        LivePaths(EvalContext& c, function_ref<void(const ValuePath&)> f) : ec(c), cb(f) {}
        template <typename T> void select(const T& x) { ValuePath::visitPaths(x, ec, cb, false); }
        void handle(const NamedValueExpression& x) { select(x); }
        void handle(const HierarchicalValueExpression& x) { select(x); }
        void handle(const ElementSelectExpression& x) { select(x); }
        void handle(const RangeSelectExpression& x) { select(x); }
        void handle(const MemberAccessExpression& x) { select(x); }
        static bool decided(const Expression& cond, EvalContext& ec, bool& value) {
            auto cv = cond.eval(ec);
            if (!cv.isInteger() || cv.hasUnknown()) return false;
            value = cv.isTrue();
            return true;
        }
        void handle(const ConditionalExpression& c) {
            bool value = false;
            if (c.conditions.size() == 1 && !c.conditions[0].pattern && decided(*c.conditions[0].expr, ec, value)) {
                c.conditions[0].expr->visit(*this);
                (value ? c.left() : c.right()).visit(*this);
                return;
            }
            visitDefault(c);
        }
        void handle(const BinaryExpression& b) {
            bool value = false;
            if ((b.op == BinaryOperator::LogicalAnd || b.op == BinaryOperator::LogicalOr) && decided(b.left(), ec, value) &&
                value == (b.op == BinaryOperator::LogicalOr)) {   // `0 && x` and `1 || x`: the right operand never decides
                b.left().visit(*this);
                return;
            }
            visitDefault(b);
        }
    };
    static void visitLivePaths(const Expression& e, EvalContext& ec, function_ref<void(const ValuePath&)> cb) {
        LivePaths v(ec, cb);
        e.visit(v);
    }
    std::vector<Path> rawPathsOf(const Expression& e, EvalContext& ec, bool skipSelectors, bool staticOnly = false) {
        std::vector<Path> out;
        auto note = [&](const ValuePath& vp) {
            const ValueSymbol* rs = vp.rootSymbol();
            if (!rs || !isGraphSymbol(*rs)) return;
            if (staticOnly && !(vp.lsp && vp.isFullyStatic())) return;
            uint64_t w = widthOf(*rs);
            Path p{rs, 0, w ? w - 1 : 0};
            if (vp.lsp && w) { p.lo = vp.lspBounds.first; p.hi = vp.lspBounds.second; }
            if (p.lo > p.hi) std::swap(p.lo, p.hi);
            out.push_back(p);
        };
        // only inside an unrolled loop iteration or a generate-loop body, where the bound loop variable or genvar makes a condition constant
        if (skipSelectors || (boundLoops.empty() && !loopGenerate)) ValuePath::visitPaths(e, ec, note, skipSelectors);
        else visitLivePaths(e, ec, note);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // Summaries collect dependency sets, not function values. Formals retain
    // the existing argument-carried behavior; locals are never graph nodes.
    struct CallSummary {
        std::set<Path> reads, writes;
        std::set<std::string> unknown;
        bool unboundedWrites = false;
    };
    std::map<const SubroutineSymbol*, CallSummary> callSummaries;
    std::set<const SubroutineSymbol*> activeCalls;
    CallSummary recursiveCall{{}, {}, {"recursive call graph is not expanded"}, true};
    CallSummary limitedCall{{}, {}, {"subroutine expansion depth limit reached"}, true};
    std::map<std::pair<const CallExpression*, const InstanceSymbol*>, uint32_t> callIssueIds;
    std::vector<uint32_t> unknownWriteCalls;

    static bool localTo(const ValueSymbol& symbol, const SubroutineSymbol& sub) {
        for (auto scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope())
            if (&scope->asSymbol() == &sub) return true;
        return false;
    }
    static bool returnsValue(const Statement& statement, const SubroutineSymbol& sub) {
        switch (statement.kind) {
            case StatementKind::Return: return statement.as<ReturnStatement>().expr != nullptr;
            case StatementKind::Block: return returnsValue(statement.as<BlockStatement>().body, sub);
            case StatementKind::List:
                for (const auto* stmt : statement.as<StatementList>().list) if (returnsValue(*stmt, sub)) return true;
                return false;
            case StatementKind::Conditional: {
                const auto& branch = statement.as<ConditionalStatement>();
                return branch.ifFalse && returnsValue(branch.ifTrue, sub) && returnsValue(*branch.ifFalse, sub);
            }
            case StatementKind::Case: {
                // Every item and a default each establish the value; a case without a default may match nothing.
                const auto& c = statement.as<CaseStatement>();
                if (!c.defaultCase || !returnsValue(*c.defaultCase, sub)) return false;
                for (const auto& item : c.items) if (!returnsValue(*item.stmt, sub)) return false;
                return true;
            }
            case StatementKind::ExpressionStatement: {
                const auto& expr = statement.as<ExpressionStatement>().expr;
                if (expr.kind != ExpressionKind::Assignment) return false;
                const auto& assignment = expr.as<AssignmentExpression>();
                return !assignment.isCompound() && assignment.left().kind == ExpressionKind::NamedValue &&
                       assignment.left().getSymbolReference() == sub.returnValVar;
            }
            default: return false;
        }
    }
    /// Collects static locals of `sub` that act solely as bounded `for`-loop induction variables:
    /// initialized on loop entry, never declared with an initializer, never written in the loop body,
    /// and never referenced outside an enclosing unrollable `for` loop that initializes them.
    std::set<const VariableSymbol*> collectPureLoopInductionLocals(const SubroutineSymbol& sub) {
        static constexpr size_t kMaxScanWalks = 2048;
        std::set<const VariableSymbol*> initialized, disqualified;
        struct Scan : ASTVisitor<Scan, VisitFlags::AllGood> {
            Builder& b;
            const SubroutineSymbol& sub;
            EvalContext& ec;
            std::set<const VariableSymbol*>& initialized;
            std::set<const VariableSymbol*>& disqualified;
            std::vector<const VariableSymbol*> active;
            size_t walkBudget = 0;
            bool budgetExceeded = false;
            Scan(Builder& builder, const SubroutineSymbol& routine, EvalContext& ctx,
                 std::set<const VariableSymbol*>& init, std::set<const VariableSymbol*>& bad)
                : b(builder), sub(routine), ec(ctx), initialized(init), disqualified(bad) {}
            bool candidate(const Symbol* s, const VariableSymbol*& out) const {
                if (!s || s->kind == SymbolKind::FormalArgument) return false;
                const auto* var = s->as_if<VariableSymbol>();
                if (!var || var == sub.returnValVar || !localTo(*var, sub)) return false;
                if (var->lifetime == VariableLifetime::Automatic) return false;
                out = var;
                return true;
            }
            bool isActive(const VariableSymbol* v) const {
                return std::find(active.rbegin(), active.rend(), v) != active.rend();
            }
            void handle(const VariableDeclStatement& decl) {
                if (const VariableSymbol* v = nullptr; candidate(&decl.symbol, v) && decl.symbol.getInitializer())
                    disqualified.insert(v);
                if (const auto* init = decl.symbol.getInitializer()) init->visit(*this);
            }
            void handle(const NamedValueExpression& expr) {
                if (const VariableSymbol* v = nullptr; candidate(&expr.symbol, v) && !isActive(v))
                    disqualified.insert(v);
            }
            void handle(const HierarchicalValueExpression& expr) {
                if (const VariableSymbol* v = nullptr; candidate(expr.getSymbolReference(), v) && !isActive(v))
                    disqualified.insert(v);
            }
            void handle(const ForLoopStatement& loop) {
                std::vector<LoopBinding> iterations;
                const bool unrolls = b.unrollForLoop(loop, ec, iterations) && !iterations.empty();
                std::vector<const VariableSymbol*> mine;
                if (unrolls) {
                    for (auto* init : loop.initializers) {
                        const auto* asg = init->as_if<AssignmentExpression>();
                        const auto* nv = asg && !asg->isCompound() ? asg->left().as_if<NamedValueExpression>() : nullptr;
                        const VariableSymbol* v = nullptr;
                        if (nv && candidate(&nv->symbol, v) && !v->getInitializer()) {
                            mine.push_back(v);
                            initialized.insert(v);
                        }
                    }
                }
                const size_t saved = active.size();
                active.insert(active.end(), mine.begin(), mine.end());
                for (auto* init : loop.initializers) init->visit(*this);
                if (loop.stopExpr) loop.stopExpr->visit(*this);
                if (unrolls && !iterations.empty()) {
                    if (budgetExceeded || walkBudget + iterations.size() > kMaxScanWalks) {
                        budgetExceeded = true;
                        for (auto* v : mine) disqualified.insert(v);
                        loop.body.visit(*this);
                    } else {
                        walkBudget += iterations.size();
                        const auto savedLoops = b.boundLoops;
                        for (const auto& binding : iterations) {
                            for (size_t k = savedLoops.size(); k < binding.size(); ++k)
                                ec.createLocal(binding[k].first, binding[k].second);
                            b.boundLoops = binding;
                            loop.body.visit(*this);
                            for (size_t k = savedLoops.size(); k < binding.size(); ++k)
                                ec.deleteLocal(binding[k].first);
                        }
                        b.boundLoops = savedLoops;
                    }
                } else {
                    loop.body.visit(*this);
                }
                for (auto* step : loop.steps) step->visit(*this);
                active.resize(saved);
            }
        };
        EvalContext ec(sub);
        Scan scan(*this, sub, ec, initialized, disqualified);
        sub.getBody().visit(scan);
        if (scan.budgetExceeded) initialized.clear();
        for (const auto* bad : disqualified) initialized.erase(bad);
        return initialized;
    }

    /// A return value built piece by piece (`f[7] = a; f[6] = b; ...` or an unconditional constant-bounded
    /// `for` loop filling `f[i]`) is established when unconditional statements cover every bit.
    bool returnAssembled(const SubroutineSymbol& sub) {
        if (!sub.returnValVar) return false;
        EvalContext ec(sub);
        std::vector<std::pair<uint64_t, uint64_t>> covered;
        size_t walkBudget = 0;
        size_t walkFactor = 1;
        bool aborted = false;
        static constexpr size_t kMaxReturnUnrollWalks = 2048;
        std::function<void(const Statement&)> collect = [&](const Statement& st) {
            if (aborted) return;
            if (st.kind == StatementKind::Block) {
                collect(st.as<BlockStatement>().body);
                return;
            }
            if (st.kind == StatementKind::List) {
                for (const auto* inner : st.as<StatementList>().list) {
                    collect(*inner);
                    if (aborted) return;
                }
                return;
            }
            if (st.kind == StatementKind::ForLoop) {
                const auto& loop = st.as<ForLoopStatement>();
                std::vector<LoopBinding> iterations;
                if (!unrollForLoop(loop, ec, iterations) || iterations.empty() ||
                    iterations.size() * walkFactor + walkBudget > kMaxReturnUnrollWalks) {
                    aborted = true;
                    return;
                }
                walkBudget += iterations.size() * walkFactor;
                const size_t savedFactor = walkFactor;
                walkFactor *= iterations.size();
                const auto savedLoops = boundLoops;
                for (const auto& binding : iterations) {
                    for (size_t k = savedLoops.size(); k < binding.size(); ++k)
                        ec.createLocal(binding[k].first, binding[k].second);
                    boundLoops = binding;
                    collect(loop.body);
                    for (size_t k = savedLoops.size(); k < binding.size(); ++k)
                        ec.deleteLocal(binding[k].first);
                    if (aborted) break;
                }
                boundLoops = savedLoops;
                walkFactor = savedFactor;
                return;
            }
            if (st.kind != StatementKind::ExpressionStatement) return;
            const auto& expr = st.as<ExpressionStatement>().expr;
            if (expr.kind != ExpressionKind::Assignment || expr.as<AssignmentExpression>().isCompound()) return;
            const auto& left = expr.as<AssignmentExpression>().left();
            if (left.getSymbolReference() != sub.returnValVar) return;
            for (const auto& path : rawPathsOf(left, ec, true, true))
                if (path.sym == sub.returnValVar) covered.emplace_back(path.lo, path.hi);
        };
        collect(sub.getBody());
        const uint64_t width = widthOf(*sub.returnValVar);
        if (!width || covered.empty()) return false;
        std::sort(covered.begin(), covered.end());
        uint64_t next = 0;
        for (const auto& [lo, hi] : covered) {
            if (lo > next) return false;
            next = std::max(next, hi + 1);
        }
        return next >= width;
    }
    struct SummaryVisitor : ASTVisitor<SummaryVisitor, VisitFlags::AllGood> {
        Builder& b;
        const SubroutineSymbol& sub;
        CallSummary& result;
        const std::set<const VariableSymbol*>& loopInductionLocals;
        EvalContext ec;
        size_t visited = 0;
        SummaryVisitor(Builder& builder, const SubroutineSymbol& routine, CallSummary& summary,
                       const std::set<const VariableSymbol*>& pureLoops)
            : b(builder), sub(routine), result(summary), loopInductionLocals(pureLoops), ec(routine) {}
        bool tick() {
            ++b.m.subroutineBodyNodes;
            if (++visited <= 4096) return true;
            result.unknown.insert("subroutine body-size limit reached"); result.unboundedWrites = true; return false;
        }
        void paths(const Expression& expr, bool write, bool skipSelectors = false) {
            for (const auto& path : b.rawPathsOf(expr, ec, skipSelectors)) {
                if (localTo(*path.sym, sub)) {
                    if (path.sym == sub.returnValVar) {
                        if (!write) result.unknown.insert("function return state is read before its value is established");
                    } else if (path.sym->kind != SymbolKind::FormalArgument) {
                        // An automatic local holds nothing between calls, and every read it could copy is already in this
                        // summary. A static local can carry state from an earlier call, unless it is solely an initialized loop induction variable.
                        // A local read before it is assigned has no signal behind it: no dependency is lost (LNT-044 reports the read).
                        const auto* local = path.sym->as_if<VariableSymbol>();
                        if (!local || (local->lifetime != VariableLifetime::Automatic && !loopInductionLocals.count(local)))
                            result.unknown.insert("static subroutine-local state is not expanded");
                    }
                    continue;
                }
                auto& set = write ? result.writes : result.reads;
                if (set.size() < 256) set.insert(path);
                else { result.unknown.insert("subroutine dependency limit reached"); result.unboundedWrites = true; }
            }
        }
        template<class T> void handle(const T& node) {
            if (!tick()) return;
            if constexpr (std::is_base_of_v<Statement, T>) {
                if constexpr (!std::is_same_v<T, StatementList> && !std::is_same_v<T, BlockStatement> &&
                              !std::is_same_v<T, ReturnStatement> && !std::is_same_v<T, ConditionalStatement> &&
                              !std::is_same_v<T, CaseStatement> && !std::is_same_v<T, ExpressionStatement> &&
                              !std::is_same_v<T, VariableDeclStatement> && !std::is_same_v<T, EmptyStatement> &&
                              !std::is_same_v<T, ForLoopStatement> && !std::is_same_v<T, WhileLoopStatement> &&
                              !std::is_same_v<T, DoWhileLoopStatement> && !std::is_same_v<T, RepeatLoopStatement> &&
                              !std::is_same_v<T, ForeachLoopStatement>)
                    result.unknown.insert("unsupported subroutine control or timing statement");
            }
            this->visitDefault(node);
        }
        void handle(const NamedValueExpression& expr) { if (tick()) paths(expr, false); }
        void handle(const HierarchicalValueExpression& expr) { if (tick()) paths(expr, false); }
        void handle(const ElementSelectExpression& expr) { if (tick()) { paths(expr, false); visitDefault(expr); } }
        void handle(const RangeSelectExpression& expr) { if (tick()) { paths(expr, false); visitDefault(expr); } }
        void handle(const MemberAccessExpression& expr) {
            if (!tick()) return;
            if (expr.value().type->isClass() || expr.value().type->isVirtualInterface())
                result.unknown.insert("object or virtual-interface state is not expanded");
            visitDefault(expr);
        }
        void handle(const VariableDeclStatement& declaration) {
            if (!tick()) return;
            if (auto* initializer = declaration.symbol.getInitializer()) initializer->visit(*this);
            if (declaration.symbol.lifetime != VariableLifetime::Automatic &&
                !loopInductionLocals.count(&declaration.symbol))
                result.unknown.insert("static subroutine-local state is not expanded");
        }
        void handle(const UnaryExpression& expr) {
            if (!tick()) return;
            if (expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Postincrement ||
                expr.op == UnaryOperator::Predecrement || expr.op == UnaryOperator::Postdecrement)
                paths(expr.operand(), true, true);
            visitDefault(expr);
        }
        // The reads of an assignment target: its select indices and bounds, not the variable it selects from.
        void visitTarget(const Expression& e) {
            switch (e.kind) {
                case ExpressionKind::ElementSelect:
                    visitTarget(e.as<ElementSelectExpression>().value());
                    e.as<ElementSelectExpression>().selector().visit(*this);
                    return;
                case ExpressionKind::RangeSelect:
                    visitTarget(e.as<RangeSelectExpression>().value());
                    e.as<RangeSelectExpression>().left().visit(*this);
                    e.as<RangeSelectExpression>().right().visit(*this);
                    return;
                case ExpressionKind::MemberAccess: visitTarget(e.as<MemberAccessExpression>().value()); return;
                case ExpressionKind::Concatenation:
                    for (const auto* operand : e.as<ConcatenationExpression>().operands()) visitTarget(*operand);
                    return;
                case ExpressionKind::NamedValue:
                case ExpressionKind::HierarchicalValue: return;
                default: e.visit(*this);
            }
        }
        void handle(const AssignmentExpression& assignment) {
            if (!tick()) return;
            paths(assignment.left(), true, true);
            // Index expressions influence a write but the LHS value itself is
            // not read by a simple assignment.
            auto targets = b.rawPathsOf(assignment.left(), ec, true);
            for (const auto& path : b.rawPathsOf(assignment.left(), ec, false))
                if (assignment.isCompound() || std::find(targets.begin(), targets.end(), path) == targets.end()) {
                    if (!localTo(*path.sym, sub)) result.reads.insert(path);
                }
            if (assignment.isCompound()) paths(assignment.left(), false);
            if (assignment.left().kind != ExpressionKind::NamedValue && assignment.left().kind != ExpressionKind::HierarchicalValue) {
                // `ret[i] = x` writes part of a local or of the return value: it reads the index, not the variable.
                const auto* root = assignment.left().getSymbolReference();
                if (root && localTo(root->as<ValueSymbol>(), sub)) visitTarget(assignment.left());
                else assignment.left().visit(*this);
            }
            assignment.right().visit(*this);
        }
        void handle(const CallExpression& call) {
            if (!tick()) return;
            this->visitDefault(call); // actual argument reads, including nested argument calls
            if (call.isSystemCall()) {
                // A value function of its arguments reads nothing else and writes nothing: its arguments are the whole effect.
                static const std::set<std::string_view> pure = {"$clog2", "$signed", "$unsigned", "$countones", "$onehot", "$onehot0",
                                                                 "$isunknown", "$bits", "$size", "$high", "$low", "$left", "$right",
                                                                 "$increment", "$dimensions"};
                if (pure.count(call.getSubroutineName()) && !call.hasOutputArgs()) return;
                result.unknown.insert("system call '" + std::string(call.getSubroutineName()) +
                    "' inside the body retains argument-only modeling");
                if (call.hasOutputArgs()) result.unboundedWrites = true;
                return;
            }
            const auto* callee = std::get<const SubroutineSymbol*>(call.subroutine);
            if (!callee || call.thisClass()) { result.unknown.insert("unresolved or class-method call"); result.unboundedWrites = true; return; }
            const auto& nested = b.summarize(*callee);
            for (const auto& path : nested.reads) {
                if (result.reads.size() < 256) result.reads.insert(path);
                else { result.unknown.insert("subroutine dependency limit reached"); result.unboundedWrites = true; }
            }
            for (const auto& path : nested.writes) {
                if (result.writes.size() < 256) result.writes.insert(path);
                else { result.unknown.insert("subroutine dependency limit reached"); result.unboundedWrites = true; }
            }
            result.unknown.insert(nested.unknown.begin(), nested.unknown.end());
            result.unboundedWrites = result.unboundedWrites || nested.unboundedWrites;
            if (call.hasOutputArgs()) {
                result.unknown.insert("output, inout or ref argument effects are not expanded");
                result.unboundedWrites = true;
                auto args = callee->getArguments();
                for (size_t i = 0; i < args.size() && i < call.arguments().size(); ++i)
                    if (args[i]->direction != ArgumentDirection::In) paths(*call.arguments()[i], true, true);
            }
        }
    };
    const CallSummary& summarize(const SubroutineSymbol& sub) {
        if (activeCalls.count(&sub)) return recursiveCall;
        if (auto it = callSummaries.find(&sub); it != callSummaries.end()) return it->second;
        if (activeCalls.size() >= 32) return limitedCall;
        activeCalls.insert(&sub);
        auto& result = callSummaries[&sub];
        ++m.subroutineSummaries;
        if (sub.flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) || sub.isVirtual()) {
            result.unknown.insert("external or virtual subroutine has no resolved body");
            // A non-context DPI import can only observe and affect its
            // arguments. Context imports, interface externs and virtual
            // dispatch may also access state outside the call.
            result.unboundedWrites = !sub.flags.has(MethodFlags::DPIImport) ||
                                     sub.flags.has(MethodFlags::DPIContext | MethodFlags::InterfaceExtern) || sub.isVirtual();
        } else {
            const auto pureLoopLocals = collectPureLoopInductionLocals(sub);
            SummaryVisitor visitor(*this, sub, result, pureLoopLocals);
            sub.getBody().visit(visitor);
            if (sub.subroutineKind == SubroutineKind::Function && !returnsValue(sub.getBody(), sub) && !returnAssembled(sub))
                result.unknown.insert("a return value is not established on every path");
        }
        if (!result.writes.empty()) result.unknown.insert("writes to nonlocal state are not expanded as procedural effects");
        if (sub.hasOutputArgs()) result.unknown.insert("output, inout or ref argument effects are not expanded");
        activeCalls.erase(&sub);
        return result;
    }
    bool callPathResolved(const Path& path) const {
        for (auto scope = path.sym->getParentScope(); scope; scope = scope->asSymbol().getParentScope())
            if (scope->asSymbol().kind == SymbolKind::Package) return true;
        const auto* body = bodyOf(*path.sym);
        if (!isModuleLevel(*path.sym) || !body) return false; // non-package local/nonlocal state needs an explicit state model
        // A summary stores symbols, not a hierarchical instance selection.
        // Shared bodies need that extra ownership fact before their state can be attached.
        auto it = bodyInstances.find(body);
        return it != bodyInstances.end() && it->second.size() == 1;
    }
    const CallSummary& summaryFor(const CallExpression& call) {
        const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
        return sub ? summarize(*sub) : limitedCall;
    }
    std::unordered_map<const Expression*, std::vector<const CallExpression*>> expressionCalls;
    const std::vector<const CallExpression*>& callsIn(const Expression& expr) {
        auto [it, inserted] = expressionCalls.try_emplace(&expr);
        if (inserted) {
            struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
                std::vector<const CallExpression*>& out;
                explicit Calls(std::vector<const CallExpression*>& calls) : out(calls) {}
                void handle(const CallExpression& call) {
                    if (!call.isSystemCall()) out.push_back(&call);
                    visitDefault(call);
                }
            } visitor(it->second);
            expr.visit(visitor);
        }
        return it->second;
    }
    // Deliberately small two-state evaluator. Calls (including system calls)
    // prevent pruning of the whole expression, preserving execution effects.

    /// The bits of `e`, most significant first: '0', '1', or '?' where no mode fixes the bit. False unless `e` only moves bits.
    bool modeBits(const Expression& e, EvalContext& ec, std::string& out) {
        const uint64_t width = e.type && e.type->isIntegral() ? e.type->getBitstreamWidth() : 0;
        if (!width || width > kMaxBits) return false;
        auto cv = e.eval(ec);
        if (cv.isInteger()) {
            if (cv.integer().getBitWidth() != width) return false;
            for (uint64_t i = width; i-- > 0;) {
                const logic_t bit = cv.integer()[static_cast<int32_t>(i)];
                out.push_back(bit.value == 0 ? '0' : bit.value == 1 ? '1' : '?');
            }
            return true;
        }
        if (e.kind == ExpressionKind::Concatenation) {
            for (auto* op : e.as<ConcatenationExpression>().operands())
                if (!modeBits(*op, ec, out)) return false;
            return true;
        }
        if (e.kind == ExpressionKind::Conversion) {
            const auto& op = e.as<ConversionExpression>().operand();
            const uint64_t from = op.type && op.type->isIntegral() ? op.type->getBitstreamWidth() : 0;
            if (!from || from > width || (from < width && op.type->isSigned())) return false;
            out.append(width - from, '0');
            return modeBits(op, ec, out);
        }
        if (e.kind != ExpressionKind::NamedValue && e.kind != ExpressionKind::ElementSelect && e.kind != ExpressionKind::RangeSelect)
            return false;
        ValuePath path(e, ec);
        const ValueSymbol* root = path.rootSymbol();
        if (!root || !path.lsp || !path.isFullyStatic()) return false;
        uint64_t lo = path.lspBounds.first, hi = path.lspBounds.second;
        if (lo > hi) std::swap(lo, hi);
        if (hi - lo + 1 != width) return false;
        const auto scalar = design.caseConstants.find(root);
        const auto vector = design.caseBits.find(root);
        for (uint64_t b = hi + 1; b-- > lo;) {
            int value = scalar != design.caseConstants.end() ? scalar->second : -1;
            if (vector != design.caseBits.end() && b < vector->second.size()) value = vector->second[b];
            out.push_back(value < 0 ? '?' : value ? '1' : '0');
        }
        return true;
    }
    int modeValue(const Expression& e, EvalContext& ec) {
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression&) { found = true; }
        } calls;
        e.visit(calls);
        if (calls.found) return -1;
        auto cv = e.eval(ec);
        if (cv.isInteger() && !cv.hasUnknown()) return cv.isFalse() ? 0 : 1;
        if (e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::ElementSelect ||
            e.kind == ExpressionKind::RangeSelect || e.kind == ExpressionKind::Concatenation) {
            std::string bits;
            if (!modeBits(e, ec, bits)) return -1;
            if (bits.find('1') != std::string::npos) return 1;   // true whatever the unfixed bits are
            return bits.find('?') != std::string::npos ? -1 : 0;
        }
        if (e.kind == ExpressionKind::Conversion) {
            const auto& op = e.as<ConversionExpression>().operand();
            if (e.type->getBitstreamWidth() >= op.type->getBitstreamWidth()) return modeValue(op, ec);
        }
        if (e.kind == ExpressionKind::UnaryOp) {
            const auto& u = e.as<UnaryExpression>();
            if (u.op == UnaryOperator::LogicalNot ||
                (u.op == UnaryOperator::BitwiseNot && u.operand().type->getBitstreamWidth() == 1)) {
                int v = modeValue(u.operand(), ec);
                return v < 0 ? -1 : !v;
            }
            std::string bits;
            const bool reduction = u.op == UnaryOperator::BitwiseAnd || u.op == UnaryOperator::BitwiseNand ||
                                   u.op == UnaryOperator::BitwiseOr || u.op == UnaryOperator::BitwiseNor ||
                                   u.op == UnaryOperator::BitwiseXor || u.op == UnaryOperator::BitwiseXnor;
            if (reduction && modeBits(u.operand(), ec, bits)) {
                const bool zero = bits.find('0') != std::string::npos, one = bits.find('1') != std::string::npos,
                           unset = bits.find('?') != std::string::npos;
                if (u.op == UnaryOperator::BitwiseAnd || u.op == UnaryOperator::BitwiseNand) {
                    if (!zero && unset) return -1;
                    return zero == (u.op == UnaryOperator::BitwiseNand);
                }
                if (u.op == UnaryOperator::BitwiseOr || u.op == UnaryOperator::BitwiseNor) {
                    if (!one && unset) return -1;
                    return one == (u.op == UnaryOperator::BitwiseOr);
                }
                if (unset) return -1;
                const bool parity = std::count(bits.begin(), bits.end(), '1') % 2;
                return parity == (u.op == UnaryOperator::BitwiseXor);
            }
        }
        if (e.kind == ExpressionKind::BinaryOp) {
            const auto& op = e.as<BinaryExpression>();
            if (op.op == BinaryOperator::Equality || op.op == BinaryOperator::Inequality ||
                op.op == BinaryOperator::CaseEquality || op.op == BinaryOperator::CaseInequality) {
                std::string left, right;
                if (!modeBits(op.left(), ec, left) || !modeBits(op.right(), ec, right) || left.size() != right.size()) return -1;
                bool unset = false;
                for (size_t i = 0; i < left.size(); ++i) {
                    if (left[i] == '?' || right[i] == '?') unset = true;
                    else if (left[i] != right[i]) return op.op == BinaryOperator::Inequality || op.op == BinaryOperator::CaseInequality;
                }
                if (unset) return -1;
                return op.op == BinaryOperator::Equality || op.op == BinaryOperator::CaseEquality;
            }
            int a = modeValue(op.left(), ec), b = modeValue(op.right(), ec);
            if (op.op == BinaryOperator::LogicalAnd) {
                if (a == 0 || b == 0) return 0;
                if (a == 1 && b == 1) return 1;
            }
            if (op.op == BinaryOperator::LogicalOr) {
                if (a == 1 || b == 1) return 1;
                if (a == 0 && b == 0) return 0;
            }
        }
        return -1;
    }
    std::unordered_set<const Expression*> prunedExpressions;
    // A `?:` whose select is an elaboration constant is not a mux: only the chosen operand exists in the hardware.
    const Expression* constantSelect(const Expression& e, EvalContext& ec) {
        const Expression* x = &e;
        while (x->kind == ExpressionKind::Conversion) {
            const auto& op = x->as<ConversionExpression>().operand();
            if (!x->type || !op.type || x->type->getBitstreamWidth() != op.type->getBitstreamWidth()) return nullptr;
            x = &op;
        }
        if (x->kind != ExpressionKind::ConditionalOp) return nullptr;
        const auto& c = x->as<ConditionalExpression>();
        if (c.conditions.size() != 1 || c.conditions[0].pattern) return nullptr;
        auto cv = c.conditions[0].expr->eval(ec);
        if (!cv.isInteger() || cv.hasUnknown()) return nullptr;
        const Expression& chosen = cv.isFalse() ? c.right() : c.left();
        if (!chosen.type || !x->type || chosen.type->getBitstreamWidth() != x->type->getBitstreamWidth()) return nullptr;
        const Expression* deeper = constantSelect(chosen, ec);
        return deeper ? deeper : &chosen;
    }
    const Expression& modeExpr(const Expression& e, EvalContext& ec) {
        if (!design.hasCaseMode()) {
            if (const auto* folded = constantSelect(e, ec)) return *folded;
            return e;
        }
        if (e.kind == ExpressionKind::Conversion) {
            // Only remove width-preserving conversions.
            const auto& op = e.as<ConversionExpression>().operand();
            if (e.type->getBitstreamWidth() == op.type->getBitstreamWidth()) return modeExpr(op, ec);
        }
        if (e.kind == ExpressionKind::ConditionalOp) {
            const auto& c = e.as<ConditionalExpression>();
            if (c.conditions.size() == 1 && !c.conditions[0].pattern) {
                int v = modeValue(*c.conditions[0].expr, ec);
                if (v >= 0) { m.casePrunedExpressions += prunedExpressions.insert(&e).second; return modeExpr(v ? c.left() : c.right(), ec); }
            }
        }
        return e;
    }
    /// Whole-net copies in one design: `wire r = rst_n;` and `assign r = rst_n;` map r to rst_n when r has no other driver
    /// and no delay. Filled per scope before its members. Multiply-driven nets (`wand alias_n; assign alias_n = rst_n; ...`)
    /// stay out of plainCopyOf and record their plain-copy sources separately so event matching still finds the clock while
    /// forcing the guard to stay conditional (round 4 A2a).
    std::unordered_map<const ValueSymbol*, const ValueSymbol*> plainCopyOf;
    std::unordered_map<const ValueSymbol*, std::vector<const ValueSymbol*>> copiesOf;   // the reverse of plainCopyOf
    std::unordered_set<const ValueSymbol*> multiDrivenSyms;
    std::unordered_map<const ValueSymbol*, std::vector<const ValueSymbol*>> multiDriverSources;
    void addPlainCopy(const ValueSymbol* copy, const ValueSymbol* source) {
        if (plainCopyOf.emplace(copy, source).second) copiesOf[source].push_back(copy);
    }
    /// The path with plain copies followed back to the net they copy (same width, same bit positions). A test of `r` is a test of
    /// `rst_n` when `r` is a copy of it: both read the same value.
    Path canonPath(Path p) const {
        for (int guard = 0; guard < 16 && p.sym; guard++) {
            auto it = plainCopyOf.find(p.sym);
            if (it == plainCopyOf.end() || widthOf(*p.sym) != widthOf(*it->second)) break;
            p.sym = it->second;
        }
        return p;
    }

    std::vector<Path> pathsOf(const Expression& expr, EvalContext& ec, bool skipSelectors) {
        const auto& selected = skipSelectors ? expr : modeExpr(expr, ec);
        // Only a one-bit value is fixed outright: a true vector can still carry unfixed bits, as in {mode[1], data}.
        if (!skipSelectors && design.hasCaseMode() && selected.type->getBitstreamWidth() == 1 && modeValue(selected, ec) >= 0) return {};
        auto result = rawPathsOf(selected, ec, skipSelectors);
        // Only expression reads are expanded. An lvalue call cannot establish
        // an ordinary target; its effects are recorded separately.
        if (!skipSelectors)
            for (const auto* call : callsIn(expr))
                for (const auto& path : summaryFor(*call).reads)
                    if (callPathResolved(path)) result.push_back(path);
        std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
    std::vector<uint32_t> callIssues(const Expression& expr, const InstanceSymbol* ctx) {
        std::vector<uint32_t> ids;
        for (const auto* call : callsIn(expr)) {
            auto id = callIssue(*call, ctx);
            if (id != UINT32_MAX) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }
    uint32_t callIssue(const CallExpression& call, const InstanceSymbol* ctx) {
        auto key = std::make_pair(&call, ctx);
        if (auto it = callIssueIds.find(key); it != callIssueIds.end()) return it->second;
        const auto& summary = summaryFor(call);
        std::set<std::string> reasons = summary.unknown;
        for (const auto& path : summary.reads) if (!callPathResolved(path)) reasons.insert("a body read has unresolved instance or package-state ownership");
        for (const auto& path : summary.writes) if (!callPathResolved(path)) reasons.insert("a body write has unresolved instance or package-state ownership");
        if (call.thisClass()) reasons.insert("class-method state is not expanded");
        if (reasons.empty()) { callIssueIds[key] = UINT32_MAX; return UINT32_MAX; }
        std::string reason;
        for (const auto& item : reasons) reason += (reason.empty() ? "" : "; ") + item;
        uint32_t id = static_cast<uint32_t>(m.callUncertainties.size());
        m.callUncertainties.push_back({std::string(call.getSubroutineName()), reason, toLoc(*sm, call.sourceRange.start())});
        callIssueIds[key] = id;
        fallback("user-function-call", "call to '" + std::string(call.getSubroutineName()) + "': " + reason, call.sourceRange.start());
        for (const auto& path : summary.writes) {
            if (callPathResolved(path)) {
                for (auto node : nodesFor(ctx, path)) m.uncertainCallDrivers[node].push_back(id);
            } else unknownWriteCalls.push_back(id);
        }
        if (summary.unboundedWrites || call.thisClass()) unknownWriteCalls.push_back(id);
        return id;
    }

    static uint64_t pathWidth(const Path& p) { return p.hi - p.lo + 1; }

    // ---------------------------------------------------------------- edges
    uint32_t addProc(ProcKind k, slang::SourceLocation l, const InstanceSymbol* ctx) {
        ProcInfo pi;
        pi.kind = k;
        pi.loc = toLoc(*sm, l);
        if (ctx) pi.instancePath = ctx->getHierarchicalPath();
        m.procs.push_back(std::move(pi));
        return static_cast<uint32_t>(m.procs.size() - 1);
    }

    const OpMap* curData = nullptr;
    const OpMap* curCtrl = nullptr;
    bool curApproximate = false;   // the assignment being emitted has a computed index or a user call

    // Per procedure, from Flow: the bit ranges of each written symbol that some read can see at their pre-activation value.
    // Null when Flow could not establish this (opaque, approximated, invalid) or the procedure never reads what it writes.
    using EntryReads = std::unordered_map<const ValueSymbol*, std::vector<flow::Range>>;
    const EntryReads* curEntryReads = nullptr;
    const std::set<const ValueSymbol*>* curProvenSelf = nullptr;   // see provenSelf; set only for always_comb / always @*

    // Block-local variables that serve only as loop counters: every write is an initialiser or step of a `for` loop, and every read is in
    // that loop (its condition, steps or body). They are no hardware state (the flow cannot always show it, for example in a loop too
    // long to unroll), so they take no part in a self-dependency or cycle. A static local read before it is written is state and is not one.
    void noteLoopCounters(const Statement& body) {
        struct Scan : ASTVisitor<Scan, VisitFlags::AllGood> {
            std::set<const ValueSymbol*> counters, spoiled, active;
            std::vector<const ValueSymbol*> headerWrites;
            bool localToProc(const ValueSymbol& s) const {
                auto* sc = s.getParentScope();
                if (!sc) return false;
                auto k = sc->asSymbol().kind;
                return k == SymbolKind::StatementBlock;
            }
            void write(const Expression& e, bool header) {
                const Expression* x = &e;
                while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
                if (x->kind != ExpressionKind::NamedValue) { visitSelectorsOnly(e); return; }
                auto& v = x->as<NamedValueExpression>().symbol;
                if (!localToProc(v)) return;
                if (header && active.count(&v)) return;
                spoiled.insert(&v);
            }
            void visitSelectorsOnly(const Expression& e) { e.visit(*this); }
            void handle(const NamedValueExpression& e) {
                if (!localToProc(e.symbol)) return;
                if (!active.count(&e.symbol)) spoiled.insert(&e.symbol);
            }
            void handle(const AssignmentExpression& a) {
                a.right().visit(*this);
                write(a.left(), inHeader);
                if (a.isCompound()) a.left().visit(*this);
            }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Predecrement ||
                    u.op == UnaryOperator::Postdecrement) write(u.operand(), inHeader);
                else visitDefault(u);
            }
            bool inHeader = false;
            void handle(const ForLoopStatement& f) {
                std::vector<const ValueSymbol*> mine;
                for (auto* i : f.initializers)
                    if (i->kind == ExpressionKind::Assignment && i->as<AssignmentExpression>().left().kind == ExpressionKind::NamedValue) {
                        auto& v = i->as<AssignmentExpression>().left().as<NamedValueExpression>().symbol;
                        if (localToProc(v) && active.insert(&v).second) { mine.push_back(&v); counters.insert(&v); }
                    }
                const bool outer = inHeader;
                inHeader = true;
                for (auto* i : f.initializers) i->visit(*this);
                inHeader = outer;
                if (f.stopExpr) f.stopExpr->visit(*this);
                f.body.visit(*this);
                inHeader = true;
                for (auto* st : f.steps) st->visit(*this);
                inHeader = outer;
                for (auto* v : mine) active.erase(v);
            }
        } scan;
        body.visit(scan);
        for (auto* v : scan.counters) if (!scan.spoiled.count(v)) m.loopVariables.insert(v);
    }

    // Variables of the procedure being built whose every read follows a whole write in the same activation (analysis/temps.h). Unlike
    // `curEntryReads` this holds where Flow is approximate (a `continue`, a loop it did not unroll, a call).
    std::set<const ValueSymbol*> curDominated;
    bool readsOwnWrite(NodeId n) const {
        if (m.nodes[n].sym && curDominated.count(m.nodes[n].sym)) return true;
        if (!curEntryReads) return false;
        auto it = curEntryReads->find(m.nodes[n].sym);
        if (it == curEntryReads->end()) return false;
        if (it->second.empty()) return true;
        if (m.collapsed(n)) return false;   // the node stands for every bit
        uint64_t bit = m.nodes[n].bit;
        return std::none_of(it->second.begin(), it->second.end(), [&](const flow::Range& r) { return r.first <= bit && bit <= r.second; });
    }

    // Variables of the procedure being built that Flow proves feed back on themselves: some assignment reads the whole variable
    // while every path still holds its pre-activation value and writes the whole variable from it, through operators that keep
    // each bit in its place. So bit k of the write depends on bit k of the read: a certain combinational loop in hardware.
    std::set<const ValueSymbol*> provenSelf;

    static bool mentions(const Expression& e, const ValueSymbol& sym) {
        struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
            const ValueSymbol& sym; bool found = false;
            explicit Find(const ValueSymbol& s) : sym(s) {}
            void handle(const NamedValueExpression& n) { if (n.getSymbolReference() == &sym) found = true; }
        } f(sym);
        e.visit(f);
        return f.found;
    }
    static const Expression& stripConversions(const Expression& e) {
        const Expression* x = &e;
        while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
        return *x;
    }
    static bool cancelsSelf(const BinaryExpression& b, const ValueSymbol& sym) {
        const Expression& l = stripConversions(b.left());
        const Expression& r = stripConversions(b.right());
        auto isSelf = [&](const Expression& e) { return e.kind == ExpressionKind::NamedValue && e.as<NamedValueExpression>().getSymbolReference() == &sym; };
        if (isSelf(l) && isSelf(r))
            return b.op == BinaryOperator::BinaryXor || b.op == BinaryOperator::BinaryXnor || b.op == BinaryOperator::Subtract;
        auto folds = [&](const Expression& e, bool ones) {
            if (mentions(e, sym)) return false;
            const ConstantValue* cv = e.getConstant();
            if (!cv || !cv->isInteger() || cv->integer().hasUnknown()) return false;
            const SVInt& v = cv->integer();
            return ones ? v.countOnes() == v.getBitWidth() : bool(v == 0);
        };
        if (b.op == BinaryOperator::BinaryAnd) return folds(b.left(), false) || folds(b.right(), false);
        if (b.op == BinaryOperator::BinaryOr) return folds(b.left(), true) || folds(b.right(), true);
        return false;
    }
    /// Every mention of `sym` in `e` reaches the result through operators in which result bit k depends on operand bit k.
    static bool bitAligned(const Expression& e, const ValueSymbol& sym) {
        if (!mentions(e, sym)) return true;
        switch (e.kind) {
            case ExpressionKind::NamedValue: return true;
            case ExpressionKind::Conversion: return bitAligned(e.as<ConversionExpression>().operand(), sym);
            case ExpressionKind::UnaryOp: {
                const auto& u = e.as<UnaryExpression>();
                return u.op == UnaryOperator::BitwiseNot && bitAligned(u.operand(), sym);
            }
            case ExpressionKind::BinaryOp: {
                const auto& b = e.as<BinaryExpression>();
                switch (b.op) {
                    case BinaryOperator::BinaryAnd: case BinaryOperator::BinaryOr: case BinaryOperator::BinaryXor:
                    case BinaryOperator::BinaryXnor: case BinaryOperator::Add: case BinaryOperator::Subtract:
                        // An operand combination that folds the self term away (`a & 0`, `a | ~0`, `a ^ a`, `a - a`) leaves no loop.
                        if (cancelsSelf(b, sym)) return false;
                        return bitAligned(b.left(), sym) && bitAligned(b.right(), sym);
                    default: return false;
                }
            }
            case ExpressionKind::ConditionalOp: {
                const auto& c = e.as<ConditionalExpression>();
                for (const auto& cond : c.conditions) if (cond.pattern || mentions(*cond.expr, sym)) return false;
                return bitAligned(c.left(), sym) && bitAligned(c.right(), sym);
            }
            default: return false;
        }
    }
    static bool bitAlignedAssignment(const AssignmentExpression& a, const ValueSymbol& sym) {
        if (a.isCompound()) {
            if (!a.op) return false;
            switch (*a.op) {
                case BinaryOperator::Add: case BinaryOperator::Subtract: case BinaryOperator::BinaryAnd:
                case BinaryOperator::BinaryOr: case BinaryOperator::BinaryXor: return !mentions(a.right(), sym) && !compoundFolds(a);
                default: return false;
            }
        }
        return bitAligned(a.right(), sym);
    }
    // `x &= 0`, `x |= ~0`, `x *= 0`: the operand folds the old value away, so nothing of it is carried.
    static bool compoundFolds(const AssignmentExpression& a) {
        if (!a.op) return false;
        const Expression* r = &stripConversions(a.right());
        // slang stores `x op= y` with the right-hand side as `<lvalue> op y`
        if (r->kind == ExpressionKind::BinaryOp) r = &stripConversions(r->as<BinaryExpression>().right());
        const ConstantValue* cv = r->getConstant();
        if (!cv || !cv->isInteger() || cv->integer().hasUnknown()) return false;
        const SVInt& v = cv->integer();
        switch (*a.op) {
            case BinaryOperator::BinaryAnd: case BinaryOperator::Multiply: return bool(v == 0);
            case BinaryOperator::BinaryOr: return v.countOnes() == v.getBitWidth();
            default: return false;
        }
    }
    // A later write that keeps each bit of the variable in its place (`x += b`, `x ^= b`, `x++`, `x = x + b`), so the earlier value still reaches the output.
    static bool carriesValue(const flow::Access& later, const ValueSymbol& sym) {
        if (later.step) return !later.asg;
        if (!later.asg) return false;
        if (later.compound) {
            if (!later.asg->op) return false;
            if (*later.asg->op == BinaryOperator::Multiply) {
                // x * even always clears bit 0 but keeps the structure otherwise; only an odd or run-time factor keeps bit k from bit k
                const Expression* r = &stripConversions(later.asg->right());
                if (r->kind == ExpressionKind::BinaryOp) r = &stripConversions(r->as<BinaryExpression>().right());
                const ConstantValue* cv = r->getConstant();
                if (cv && cv->isInteger() && !cv->integer().hasUnknown()) return !compoundFolds(*later.asg) && cv->integer().getBitWidth() > 0 && cv->integer()[0] == slang::logic_t(true);
                return !mentions(later.asg->right(), sym);
            }
            return bitAlignedAssignment(*later.asg, sym);
        }
        return mentions(later.asg->right(), sym) && bitAlignedAssignment(*later.asg, sym);
    }
    static bool alignedAccess(const flow::Access& a, const ValueSymbol& sym) {
        if (a.step) return !a.asg;   // a ++ / -- nested in a larger expression is not followed
        return a.asg && bitAlignedAssignment(*a.asg, sym);
    }

    std::optional<EntryReads> entryReadsOf(const ProceduralBlockSymbol& pb, EvalContext& ec, const std::vector<Assign>& assigns) {
        provenSelf.clear();
        std::set<const ValueSymbol*> targets;
        for (auto& a : assigns) for (auto& t : a.targets) targets.insert(t.sym);
        bool feedbackCandidate = false;
        for (auto& a : assigns) {
            for (auto* reads : {&a.dataReads, &a.ctrlReads})
                for (auto& r : *reads) feedbackCandidate = feedbackCandidate || targets.count(r.sym);
        }
        if (!feedbackCandidate) return std::nullopt;
        flow::Flow fl(pb);
        if (fl.timing || fl.approximated || fl.bad) return std::nullopt;
        EntryReads entry;
        for (auto* sym : fl.writtenSymbols()) entry[sym];
        // A call is opaque to Flow, but its summary says what it can touch: the arguments are ordinary reads (Flow records those),
        // and the state its body reads counts as a read of the entry value for the bits Flow cannot show written before the call.
        // What the call writes is not followed: a later read of it still counts as a read of the entry value.
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            std::vector<const CallExpression*> found;
            void handle(const CallExpression& call) {
                if (!call.isSystemCall()) found.push_back(&call);
                visitDefault(call);
            }
        } calls;
        pb.getBody().visit(calls);
        for (const auto* call : calls.found) {
            const auto& summary = summaryFor(*call);
            // A call that writes state always carries these two notes: the write set is known, only its dependencies are not followed.
            const bool known = std::all_of(summary.unknown.begin(), summary.unknown.end(), [](const std::string& why) {
                return why == "writes to nonlocal state are not expanded as procedural effects" ||
                       why == "output, inout or ref argument effects are not expanded";
            });
            if (!known || summary.unboundedWrites || call->thisClass()) return std::nullopt;
            const auto* routine = std::get_if<const SubroutineSymbol*>(&call->subroutine);
            if (!routine || !*routine) return std::nullopt;
            for (const auto* formal : (*routine)->getArguments())
                if (formal->direction == ArgumentDirection::Ref) return std::nullopt;
            for (const auto& write : summary.writes)
                if (!callPathResolved(write)) return std::nullopt;
            // Flow sees an inout argument only as a write, but the callee reads its value first.
            const auto formals = (*routine)->getArguments();
            for (size_t i = 0; i < formals.size() && i < call->arguments().size(); i++) {
                if (formals[i]->direction != ArgumentDirection::InOut) continue;
                const Expression* arg = call->arguments()[i];
                if (arg->kind == ExpressionKind::Assignment) arg = &arg->as<AssignmentExpression>().left();
                for (const auto& path : pathsOf(*arg, ec, false)) {
                    auto it = entry.find(path.sym);
                    if (it == entry.end()) continue;
                    for (const auto& r : fl.entryRangesAtCall(*call, *path.sym))
                        if (r.first <= path.hi && path.lo <= r.second) it->second.push_back({std::max<uint64_t>(r.first, path.lo), std::min<uint64_t>(r.second, path.hi)});
                }
            }
            for (const auto& read : summary.reads) {
                if (!callPathResolved(read)) return std::nullopt;
                auto it = entry.find(read.sym);
                if (it == entry.end()) continue;
                for (const auto& r : fl.entryRangesAtCall(*call, *read.sym))
                    if (r.first <= read.hi && read.lo <= r.second) it->second.push_back({std::max<uint64_t>(r.first, read.lo), std::min<uint64_t>(r.second, read.hi)});
            }
        }
        for (const auto& access : fl.accesses)
            if (!access.write && access.entryMaybe)
                if (auto it = entry.find(access.sym); it != entry.end()) it->second.push_back(access.range);
        if (fl.opaque) return entry;
        for (uint32_t i = 0; i < fl.accesses.size(); i++) {
            const auto& r = fl.accesses[i];
            if (r.write || r.local || !r.entryOnly || r.inLoop || !r.whole || !(r.asg || r.step) || provenSelf.count(r.sym)) continue;
            for (uint32_t j = i + 1; j < fl.accesses.size(); j++) {
                const auto& w = fl.accesses[j];
                if (!w.write || w.sym != r.sym || w.asg != r.asg || w.step != r.step || !w.whole || w.inLoop) continue;
                // The loop exists in hardware only if the entry value still reaches the output through this write. A later write
                // that reads the variable bit by bit composes the value instead of replacing it (`a = a ^ x; a = a & m;`), so it
                // is transparent. Any other later write replaces it, unless this write is still the final one on some path.
                bool last = true;
                for (uint32_t k = j + 1; k < fl.accesses.size() && last; k++) {
                    const auto& later = fl.accesses[k];
                    if (!later.write || later.sym != r.sym || fl.exclusive(w, later)) continue;
                    const bool carries = later.whole && !later.inLoop && carriesValue(later, *r.sym);
                    if (!carries) last = w.live;
                }
                if (last && alignedAccess(r, *r.sym)) provenSelf.insert(r.sym);
                break;
            }
        }
        return entry;
    }

    // Every source reaches every target, so a node between them says the same with sources + targets edges, not their product.
    // A hub stands for no signal; its edges are inexact and operator Other, which every rule already treats as logic.
    void viaHub(const std::vector<NodeId>& sources, const std::unordered_set<NodeId>& targets, EdgeType type, uint32_t proc, bool ctrl,
                const InstanceSymbol* ctx) {
        const NodeId hub = static_cast<NodeId>(m.nodes.size());
        m.nodes.push_back({ctx, nullptr, 0});
        for (auto source : sources) {
            if (targets.count(source)) addEdge(source, source, type, proc, false, ctrl);   // keeps the self-dependency facts
            addEdge(source, hub, EdgeType::Comb, proc, false);
        }
        for (auto target : targets) {
            if (type == EdgeType::Comb) noteCombTarget(target, proc);
            Edge e;
            e.from = hub; e.to = target; e.proc = proc; e.type = type; e.ctrl = ctrl; e.approximate = curApproximate;
            e.op = Op::Other;
            e.inv = 2;
            pushEdge(e);
        }
    }

    bool curShared = false;   // the assignment being emitted is conditional or mixes signals (Edge::shared)
    bool curDeadWrite = false;
    std::unordered_set<Model::DeadEdgeKey, Model::DeadEdgeHash>* curLiveProcEdges = nullptr;
    void addEdge(NodeId from, NodeId to, EdgeType t, uint32_t proc, bool exact, bool ctrl = false, bool flip = false) {
        if (t == EdgeType::Comb) noteCombTarget(to, proc);
        if (from == to) {
            if (t == EdgeType::Seq && !ctrl) {
                if (exact) m.selfExact.insert(from);
                else m.selfLogic.insert(from);
            }
            if (t == EdgeType::Comb && !exact && !curDeadWrite)
                m.combSelfRewritten.insert(to);
            if (t == EdgeType::Comb && !exact && !readsOwnWrite(from) && !(m.nodes[from].sym && m.loopVariables.count(m.nodes[from].sym))) {
                if (m.selfComb.emplace(from, proc).second && curApproximate) m.selfCombApproximate.insert(from);
                else if (!curApproximate) m.selfCombApproximate.erase(from);
                if (!curApproximate && curProvenSelf && curProvenSelf->count(m.nodes[from].sym) && m.selfComb[from] == proc) m.selfCombProven.insert(from);
            }
            return;
        }
        Edge e;
        e.from = from; e.to = to; e.proc = proc; e.type = t; e.exact = exact; e.ctrl = ctrl; e.flip = flip;
        e.approximate = curApproximate;
        e.shared = curShared;
        e.internalRead = t == EdgeType::Comb && readsOwnWrite(from);
        if (const OpMap* om = ctrl ? curCtrl : curData) {
            auto it = om->find(m.nodes[from].sym);
            if (it != om->end()) {
                e.op = it->second.op;
                e.inv = it->second.inv;
                e.branch = it->second.br;
            }
        }
        const Model::DeadEdgeKey dk{from, to, proc, static_cast<uint8_t>(t)};
        if (curDeadWrite) {
            if (!curLiveProcEdges || !curLiveProcEdges->count(dk)) m.deadEdges.insert(dk);
        } else {
            if (curLiveProcEdges) curLiveProcEdges->insert(dk);
            m.deadEdges.erase(dk);
        }
        pushEdge(e);
    }

    // A vector that doubles holds both blocks while it copies. Most edges of a large design are repeats, so remove them first
    // and only grow when that frees too little.
    void pushEdge(const Edge& e) {
        if (m.edges.size() == m.edges.capacity() && m.edges.size() >= kCompactEdgesFrom) {
            m.compactEdges();
            if (m.edges.size() > m.edges.capacity() / 4 * 3) m.edges.reserve(m.edges.capacity() + m.edges.capacity() / 2);
        }
        m.edges.push_back(e);
    }

    NodeId nodeForBit(const InstanceSymbol* ctx, const BitRef& b) {
        const InstanceSymbol* inst = ownerOf(*b.sym, ctx);
        uint64_t w = widthOf(*b.sym);
        if (w == 0 || collapse(*b.sym, w)) return node(inst, b.sym, 0);
        return node(inst, b.sym, static_cast<uint32_t>(std::min<uint64_t>(b.bit, w - 1)));
    }

    /// All-to-all (inexact) dependency from `reads` to `targets`.
    void connect(const InstanceSymbol* ctx, const std::vector<Path>& reads, const InstanceSymbol* tctx,
                 const std::vector<Path>& targets, EdgeType type, uint32_t proc, slang::SourceLocation loc,
                 bool ctrl = false) {
        std::vector<std::vector<NodeId>> rn, tn;
        uint64_t rcount = 0, tcount = 0;
        for (auto& r : reads) { rn.push_back(nodesFor(ctx, r)); rcount += rn.back().size(); }
        for (auto& t : targets) { tn.push_back(nodesFor(tctx, t)); tcount += tn.back().size(); }
        if (rcount + tcount > kMaxEdgesPerAssign) {
            fallback("edge-explosion", "dependency set of " + std::to_string(rcount) + " x " + std::to_string(tcount) +
                     " bits exceeds the limit; edges truncated, targets are marked as having unresolved drivers", loc);
            for (auto& tv : tn)
                for (auto t : tv) pendingFlags.push_back({t, 4});
            return;
        }
        if (rcount * tcount > kHubEdgeThreshold && type != EdgeType::AsyncCtl) {
            std::unordered_set<NodeId> targets;
            for (auto& tv : tn) targets.insert(tv.begin(), tv.end());
            std::vector<NodeId> sources;
            for (auto& rv : rn) sources.insert(sources.end(), rv.begin(), rv.end());
            viaHub(sources, targets, type, proc, ctrl, ctx);
            return;
        }
        for (auto& rv : rn)
            for (auto r : rv)
                for (auto& tv : tn)
                    for (auto t : tv) addEdge(r, t, type, proc, false, ctrl);
    }

    /// Emits the edges of one assignment: exact bit pairs when available, inexact otherwise, plus control.
    void emitAssign(const InstanceSymbol* ctx, const Assign& a, const std::vector<Path>& targets, EdgeType type,
                    uint32_t proc, slang::SourceLocation loc, const std::set<NodeId>* skipReads = nullptr,
                    const std::set<NodeId>* skipCtrlReads = nullptr) {
        if (!a.uncertainCalls.empty()) for (const auto& target : targets) for (auto node : nodesFor(ctx, target)) {
            auto& issues = m.uncertainCallDrivers[node];
            issues.insert(issues.end(), a.uncertainCalls.begin(), a.uncertainCalls.end());
        }
        for (const auto& [site, paths] : a.conditions) {
            ConditionSite condition{site, proc, {}};
            for (const auto& path : paths)
                for (auto node : nodesFor(ctx, path)) condition.reads.push_back(node);
            if (!condition.reads.empty()) m.conditionSites.push_back(std::move(condition));
        }
        for (const auto& [site, paths, call, unresolved] : a.calledConditions) {
            ConditionSite condition{site, proc, {}, call, unresolved};
            for (const auto& path : paths)
                for (auto node : nodesFor(ctx, path)) condition.reads.push_back(node);
            if (!condition.reads.empty()) m.conditionSites.push_back(std::move(condition));
        }
        curData = &a.dataOps;
        curCtrl = &a.ctrlOps;
        curApproximate = a.approximate;
        {   // conditional, or built from several signals: not a plain copy of any one of them
            std::set<const ValueSymbol*> sources;
            for (const auto& read : a.dataReads) sources.insert(read.sym);
            curShared = a.afterPossibleExit || !a.conditions.empty() || !a.calledConditions.empty() || !a.ctrlReads.empty() || !a.booleanGuards.empty() || sources.size() > 1;
        }
        const bool unconditionalConst = !a.afterPossibleExit && a.conditions.empty() && a.calledConditions.empty() &&
                                        a.ctrlReads.empty() && a.booleanGuards.empty() && !a.approximate;
        struct Reset { Builder& b; ~Reset() { b.curData = b.curCtrl = nullptr; b.curApproximate = false; b.curShared = false; } } reset{*this};
        if (a.pairsValid) {
            for (auto& [t, src] : a.pairs) {
                if (!src.sym && t.sym) {   // a constant bit; its value is '?' when this analysis could not read it (a struct default)
                    bool sel = false;
                    for (auto& tp : targets) if (tp.sym == t.sym) sel = true;
                    if (sel && !curDeadWrite) {
                        const NodeId tn = nodeForBit(ctx, t);
                        if (type == EdgeType::Comb) noteCombTarget(tn, proc);
                        m.constDrivers[tn].push_back({src.value ? src.value : '?', type, proc, unconditionalConst});
                    }
                    continue;
                }
                if (!src.sym || !t.sym) continue;
                bool tsel = false;
                for (auto& tp : targets) if (tp.sym == t.sym) tsel = true;
                if (!tsel) continue;
                NodeId sn = nodeForBit(ctx, src);
                if (skipReads && skipReads->count(sn)) continue;
                addEdge(sn, nodeForBit(ctx, t), type, proc, true);
            }
        } else if (a.flipPairsValid) {
            for (auto& [t, src] : a.pairs) {
                bool tsel = false;
                for (auto& tp : targets) if (tp.sym == t.sym) tsel = true;
                if (!tsel || !src.sym || !t.sym) continue;
                NodeId sn = nodeForBit(ctx, src);
                if (skipReads && skipReads->count(sn)) continue;
                addEdge(sn, nodeForBit(ctx, t), type, proc, false, false, true);
            }
        } else if (a.bitDepsValid) {
            // Bits with one large read set (a leaf expression, a shared select) reach it through one hub.
            std::map<std::vector<Path>, std::vector<NodeId>> sharedReads;
            for (size_t index = 0; index < a.bitDeps.size(); index++) {
                const auto& [t, reads] = a.bitDeps[index];
                if (std::none_of(targets.begin(), targets.end(), [&](const Path& tp) { return tp.sym == t.sym; })) continue;
                const NodeId target = nodeForBit(ctx, t);
                if (reads.empty() && !curDeadWrite) {
                    if (type == EdgeType::Comb) noteCombTarget(target, proc);
                    m.constDrivers[target].push_back({'?', type, proc});   // e.g. a literal padding bit
                }
                uint64_t readBits = 0;
                for (const auto& read : reads) readBits += read.hi - read.lo + 1;
                if (readBits >= kHubReadSet) { sharedReads[reads].push_back(target); continue; }
                const bool exact = index < a.bitDepExact.size() && a.bitDepExact[index];
                if (index < a.bitMoves.size() && a.bitMoves[index].first.sym) {
                    // a copy of one source bit, plain or inverted: no other read of the expression reaches this bit
                    const NodeId source = nodeForBit(ctx, a.bitMoves[index].first);
                    if (!skipReads || !skipReads->count(source)) addEdge(source, target, type, proc, a.bitMoves[index].second == 1, false, a.bitMoves[index].second == 2);
                    continue;
                }
                for (const auto& read : reads)
                    for (auto source : nodesFor(ctx, read))
                        if (!skipReads || !skipReads->count(source)) addEdge(source, target, type, proc, exact);
            }
            for (const auto& [reads, targetBits] : sharedReads) {
                std::vector<NodeId> sources;
                for (const auto& read : reads)
                    for (auto source : nodesFor(ctx, read))
                        if (!skipReads || !skipReads->count(source)) sources.push_back(source);
                viaHub(sources, std::unordered_set<NodeId>(targetBits.begin(), targetBits.end()), type, proc, false, ctx);
            }
        } else {
            std::vector<Path> data;
            for (auto& r : a.dataReads) {
                if (skipReads) {
                    bool skip = false;
                    for (auto n : nodesFor(ctx, r)) if (skipReads->count(n)) skip = true;
                    if (skip) continue;
                }
                data.push_back(r);
            }
            connect(ctx, data, ctx, targets, type, proc, loc);
            // An assignment that reads nothing writes a constant (`x.f = 1'b1`, `v = '{default: C}`): a symbol too wide to track bit by bit
            // has no exact pairs to record that, and without it a node driven only by constants would read as undriven.
            if (a.dataReads.empty() && !a.pairsValid && !a.bitDepsValid && !curDeadWrite)
                for (const auto& target : targets)
                    for (auto node : nodesFor(ctx, target)) {
                        if (type == EdgeType::Comb) noteCombTarget(node, proc);
                        m.constDrivers[node].push_back({'?', type, proc});
                    }
        }
        if (!a.preciseBits.empty()) {
            curApproximate = false;
            for (const auto& [t, src] : a.preciseBits) addEdge(nodeForBit(ctx, src), nodeForBit(ctx, t), type, proc, false);
            curApproximate = a.approximate;
        }
        if (!a.preciseReads.empty()) {
            curApproximate = false;
            connect(ctx, a.preciseReads, ctx, targets, type, proc, loc);
            curApproximate = a.approximate;
        }
        std::vector<Path> ctrl;
        const std::set<NodeId>* ctrlSkip = skipCtrlReads ? skipCtrlReads : skipReads;
        for (auto& r : a.ctrlReads) {
            if (ctrlSkip) {
                bool skip = false;
                for (auto n : nodesFor(ctx, r)) if (ctrlSkip->count(n)) skip = true;
                if (skip) continue;
            }
            ctrl.push_back(r);
        }
        connect(ctx, ctrl, ctx, targets, type, proc, loc, true);
        if (type == EdgeType::Comb) {
            for (const auto& target : targets) for (auto node : nodesFor(ctx, target))
                m.booleanDrivers[node].push_back({a.booleanValue, !a.afterPossibleExit && a.booleanGuards.empty() && a.ctrlReads.empty()});
        }
        if (type == EdgeType::Seq) {
            bool hasData = false;
            if (a.pairsValid) { for (auto& pr : a.pairs) hasData = hasData || (pr.second.sym != nullptr); }
            else hasData = !a.dataReads.empty();
            if (hasData) {
                std::vector<NodeId> g;
                for (auto& r : ctrl) for (auto n : nodesFor(ctx, r)) g.push_back(n);
                for (auto& t : targets) for (auto n : nodesFor(ctx, t)) m.guards[n].push_back(g);
                std::vector<GuardCondition> conditions;
                for (const auto& guard : a.booleanGuards) {
                    bool resetOnly = skipReads && !guard.dependencies.empty() &&
                        std::all_of(guard.dependencies.begin(), guard.dependencies.end(), [&](NodeId n) { return skipReads->count(n); });
                    if (!resetOnly) conditions.push_back(guard);
                }
                auto dependencyNodes = [&](const std::vector<Path>& paths) {
                    auto nodes = std::make_shared<std::vector<NodeId>>();
                    for (const auto& path : paths)
                        for (auto source : nodesFor(ctx, path))
                            if (!skipReads || !skipReads->count(source)) nodes->push_back(source);
                    std::sort(nodes->begin(), nodes->end());
                    nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
                    return nodes;
                };
                const auto controlDependencies = dependencyNodes(ctrl);
                auto filterGuards = [&](const std::vector<GuardCondition>& rawGuards) {
                    std::vector<GuardCondition> out;
                    for (const auto& guard : rawGuards) {
                        bool resetOnly = skipReads && !guard.dependencies.empty() &&
                            std::all_of(guard.dependencies.begin(), guard.dependencies.end(), [&](NodeId n) { return skipReads->count(n); });
                        if (!resetOnly) out.push_back(guard);
                    }
                    return out;
                };
                if (!a.qualifierArms.empty()) {
                    for (const auto& [target, arms] : a.qualifierArms) {
                        for (const auto& arm : arms) {
                            const bool hasArmData = (arm.dataDependencies && !arm.dataDependencies->empty()) ||
                                                    arm.dataApproximate || arm.uncertainCall || !a.uncertainCalls.empty();
                            if (!hasArmData) continue;
                            std::vector<GuardCondition> mergedGuards = conditions;
                            auto armGuards = filterGuards(arm.guards);
                            mergedGuards.insert(mergedGuards.end(), armGuards.begin(), armGuards.end());
                            auto mergedCtrl = std::make_shared<std::vector<NodeId>>(*controlDependencies);
                            if (arm.controlDependencies) {
                                for (auto n : *arm.controlDependencies)
                                    if (!skipReads || !skipReads->count(n)) mergedCtrl->push_back(n);
                                std::sort(mergedCtrl->begin(), mergedCtrl->end());
                                mergedCtrl->erase(std::unique(mergedCtrl->begin(), mergedCtrl->end()), mergedCtrl->end());
                            }
                            auto armData = std::make_shared<std::vector<NodeId>>();
                            if (arm.dataDependencies) {
                                for (auto n : *arm.dataDependencies)
                                    if (!skipReads || !skipReads->count(n)) armData->push_back(n);
                            }
                            if (armData->empty() && !arm.dataApproximate && !arm.uncertainCall && a.uncertainCalls.empty()) continue;
                            m.qualifierGuards[target].push_back({std::make_shared<const std::vector<GuardCondition>>(std::move(mergedGuards)),
                                                                 std::move(armData), std::move(mergedCtrl),
                                                                 a.approximate || arm.dataApproximate,
                                                                 arm.controlApproximate,
                                                                 !a.uncertainCalls.empty() || arm.uncertainCall});
                        }
                    }
                } else {
                    const bool perBit = a.pairsValid || a.flipPairsValid || a.bitDepsValid;
                    const auto sharedConditions = std::make_shared<const std::vector<GuardCondition>>(conditions);
                    auto record = [&](NodeId target, std::shared_ptr<const std::vector<NodeId>> dataDependencies) {
                        m.qualifierGuards[target].push_back({sharedConditions, std::move(dataDependencies), controlDependencies,
                                                             a.approximate, !a.uncertainCalls.empty(), !a.uncertainCalls.empty()});
                    };
                    if (perBit) {
                        auto selected = [&](const BitRef& bit) {
                            return bit.sym && std::any_of(targets.begin(), targets.end(),
                                [&](const Path& target) { return target.sym == bit.sym; });
                        };
                        for (const auto& [target, source] : a.pairs) {
                            if (!selected(target) || !source.sym) continue;
                            const auto src = nodeForBit(ctx, source);
                            if (skipReads && skipReads->count(src)) continue;
                            record(nodeForBit(ctx, target), std::make_shared<const std::vector<NodeId>>(1, src));
                        }
                        for (const auto& [target, reads] : a.bitDeps)
                            if (selected(target) && !reads.empty()) record(nodeForBit(ctx, target), dependencyNodes(reads));
                    } else {
                        const auto dataDependencies = dependencyNodes(a.dataReads);
                        for (const auto& target : targets)
                            for (auto node : nodesFor(ctx, target)) record(node, dataDependencies);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- bit lists
    /// A pattern lists its elements most significant first and keeps them apart, like a concatenation, when they fill the packed value.
    static std::span<const Expression* const> patternElements(const Expression& e) {
        return e.kind == ExpressionKind::SimpleAssignmentPattern ? e.as<SimpleAssignmentPatternExpression>().elements()
                                                                 : e.as<StructuredAssignmentPatternExpression>().elements();
    }
    static bool isBitwisePattern(const Expression& e) {
        if (e.kind != ExpressionKind::SimpleAssignmentPattern && e.kind != ExpressionKind::StructuredAssignmentPattern) return false;
        if (!e.type || !e.type->isIntegral()) return false;
        uint64_t total = 0;
        for (const auto* element : patternElements(e)) {
            if (!element->type || !element->type->isIntegral()) return false;
            total += element->type->getBitstreamWidth();
        }
        return total == e.type->getBitstreamWidth();
    }

    /// The operand of a bitwise inversion, `~x`, `!x` on one bit, or a 1-bit constant XOR/XNOR polarity inversion (`x ^ 1'b1`, `x ~^ 1'b0`); null otherwise.
    const Expression* inversionOperand(const Expression& e, EvalContext& ec) {
        const Expression* x = &modeExpr(e, ec);
        if (!x->type || !x->type->isIntegral()) return nullptr;
        const uint64_t width = x->type->getBitstreamWidth();
        if (x->kind == ExpressionKind::UnaryOp) {
            const auto& u = x->as<UnaryExpression>();
            if (u.op == UnaryOperator::BitwiseNot && u.operand().type->getBitstreamWidth() == width) return &u.operand();
            if (u.op == UnaryOperator::LogicalNot && width == 1 && u.operand().type->getBitstreamWidth() == 1) return &u.operand();
            return nullptr;
        }
        if (x->kind == ExpressionKind::BinaryOp && width == 1) {
            const auto& b = x->as<BinaryExpression>();
            if (b.op == BinaryOperator::BinaryXor || b.op == BinaryOperator::BinaryXnor) {
                const uint64_t want = b.op == BinaryOperator::BinaryXor ? 1 : 0;
                auto isConstBit = [&](const Expression& op, uint64_t val) {
                    EvalContext tmp(ec.astCtx);
                    ConstantValue cv = op.eval(tmp);
                    if (!cv.isInteger() || cv.integer().hasUnknown()) return false;
                    auto v = cv.integer().as<uint64_t>();
                    return v && *v == val;
                };
                auto oneBitOperand = [&](const Expression& op) -> const Expression* {
                    const Expression* p = &modeExpr(op, ec);
                    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit())
                        p = &modeExpr(p->as<ConversionExpression>().operand(), ec);
                    return (p->type && p->type->isIntegral() && p->type->getBitstreamWidth() == 1) ? p : nullptr;
                };
                if (isConstBit(b.right(), want))
                    if (const Expression* s = oneBitOperand(b.left())) return s;
                if (isConstBit(b.left(), want))
                    if (const Expression* s = oneBitOperand(b.right())) return s;
            }
        }
        return nullptr;
    }
    /// `x` for `~~x`: a double inversion is the identity on every two-state bit (the model treats an inverter pair as a buffer).
    const Expression* doubleInversion(const Expression& e, EvalContext& ec) {
        const Expression* inner = inversionOperand(e, ec);
        const Expression* again = inner ? inversionOperand(*inner, ec) : nullptr;
        return again && again->type && e.type && again->type->getBitstreamWidth() == e.type->getBitstreamWidth() ? again : nullptr;
    }
    /// The bit sources (MSB first) of `~x` when `x` is a pure bit move of design signals; each target bit takes its source inverted.
    bool invertedBitMove(const Expression& e, EvalContext& ec, std::vector<BitRef>& out) {
        const Expression* operand = inversionOperand(e, ec);
        if (!operand) return false;
        if (!bitList(*operand, ec, out)) return false;
        return std::all_of(out.begin(), out.end(), [](const BitRef& bit) { return bit.sym != nullptr; });
    }

    /// Expands an expression into its ordered (MSB first) bit sources when it is a pure bit move:
    /// static selects of one symbol, concatenations, constants and zero-extension. Returns false otherwise.
    bool bitList(const Expression& e, EvalContext& ec, std::vector<BitRef>& out) {
        const Expression* x = &modeExpr(e, ec);
        if (design.hasCaseMode() && x->type->getBitstreamWidth() == 1) {
            int v = modeValue(*x, ec);
            if (v >= 0) { out.push_back({nullptr, 0, v ? '1' : '0'}); return true; }
        }
        uint64_t want = x->type ? x->type->getBitstreamWidth() : 0;
        if (x->kind == ExpressionKind::Conversion && x->as<ConversionExpression>().isImplicit()) {
            const Expression& op = x->as<ConversionExpression>().operand();
            uint64_t ow = op.type ? op.type->getBitstreamWidth() : 0;
            // A sized assignment conversion of a literal is an exact constant too
            // (including truncation, sign extension and four-state values).
            auto cv = x->eval(ec);
            if (want && cv.isInteger()) {
                for (uint64_t i = 0; i < want; i++) {
                    logic_t bit = cv.integer()[static_cast<int32_t>(want - 1 - i)];
                    out.push_back({nullptr, 0, bit.value == 0 ? '0' : bit.value == 1 ? '1' :
                                                bit.value == logic_t::z.value ? 'z' : 'x'});
                }
                return true;
            }
            if (!want || !ow || ow > want) return false;
            if (ow < want && op.type->isSigned()) return false;   // sign-extension replicates the MSB
            for (uint64_t i = ow; i < want; i++) out.push_back({nullptr, 0, '0'});   // zero extension
            return bitList(op, ec, out);
        }
        bool isConst = x->getConstant() != nullptr;
        if (!isConst && x->kind != ExpressionKind::Concatenation && x->kind != ExpressionKind::Conversion) {
            // literals and parameters: the cached constant is not always populated, so evaluate on demand
            EvalContext tmp(ec.astCtx);
            isConst = !x->eval(tmp).bad();
        }
        if (isConst) {
            EvalContext tmp(ec.astCtx);
            ConstantValue cv = x->eval(tmp);
            for (uint64_t i = 0; i < want; i++) {
                BitRef b;
                if (cv.isInteger() && want - 1 - i < cv.integer().getBitWidth()) {
                    logic_t bit = cv.integer()[static_cast<int32_t>(want - 1 - i)];
                    b.value = bit.value == 0 ? '0' : bit.value == 1 ? '1' : bit.value == logic_t::z.value ? 'z' : 'x';
                }
                out.push_back(b);
            }
            return want != 0;
        }
        if (x->kind == ExpressionKind::Concatenation) {
            for (auto* op : x->as<ConcatenationExpression>().operands())
                if (!bitList(*op, ec, out)) return false;
            return true;
        }
        if (isBitwisePattern(*x)) {
            for (auto* element : patternElements(*x))
                if (!bitList(*element, ec, out)) return false;
            return true;
        }
        if (const Expression* twice = doubleInversion(*x, ec)) return bitList(*twice, ec, out);   // ~~x is x
        if (x->kind == ExpressionKind::BinaryOp && want == 1 && x->type && x->type->isIntegral()) {
            const auto& b = x->as<BinaryExpression>();
            if (b.op == BinaryOperator::BinaryXor || b.op == BinaryOperator::BinaryXnor) {
                const uint64_t idVal = b.op == BinaryOperator::BinaryXor ? 0 : 1;
                auto isConstBit = [&](const Expression& op, uint64_t val) {
                    EvalContext tmp(ec.astCtx);
                    ConstantValue cv = op.eval(tmp);
                    if (!cv.isInteger() || cv.integer().hasUnknown()) return false;
                    auto v = cv.integer().as<uint64_t>();
                    return v && *v == val;
                };
                auto oneBitOperand = [&](const Expression& op) -> const Expression* {
                    const Expression* p = &modeExpr(op, ec);
                    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit())
                        p = &modeExpr(p->as<ConversionExpression>().operand(), ec);
                    return (p->type && p->type->isIntegral() && p->type->getBitstreamWidth() == 1) ? p : nullptr;
                };
                if (isConstBit(b.right(), idVal))
                    if (const Expression* s = oneBitOperand(b.left())) return bitList(*s, ec, out);
                if (isConstBit(b.left(), idVal))
                    if (const Expression* s = oneBitOperand(b.right())) return bitList(*s, ec, out);
            }
        }
        if (x->kind == ExpressionKind::Streaming || x->kind == ExpressionKind::Replication) return false;
        ValuePath vp(*x, ec);
        const ValueSymbol* rs = vp.rootSymbol();
        if (!rs || !isGraphSymbol(*rs) || !vp.lsp || !vp.isFullyStatic() || !want) return false;
        uint64_t lo = vp.lspBounds.first, hi = vp.lspBounds.second;
        if (lo > hi) std::swap(lo, hi);
        if (hi - lo + 1 != want) return false;
        for (uint64_t b = hi + 1; b-- > lo;) out.push_back({rs, static_cast<uint32_t>(b)});
        return true;
    }

    /// The bit sources of `x & mask` or `x | mask` where the mask is a known constant, most significant bit first: a bit the mask
    /// passes is `x`'s own bit, a bit it forces is a constant. A constant x/z mask bit is undecided ('?'). False if not this shape.
    bool maskedBits(const Expression& e, EvalContext& ec, std::vector<BitRef>& out) {
        if (e.kind != ExpressionKind::BinaryOp) return false;
        const auto& bin = e.as<BinaryExpression>();
        if (bin.op != BinaryOperator::BinaryAnd && bin.op != BinaryOperator::BinaryOr) return false;
        const bool isAnd = bin.op == BinaryOperator::BinaryAnd;
        const Expression* mask = &bin.right();
        const Expression* other = &bin.left();
        auto mv = mask->eval(ec);
        if (!mv.isInteger()) {
            std::swap(mask, other);
            mv = mask->eval(ec);
            if (!mv.isInteger()) return false;
        }
        const uint64_t want = e.type ? e.type->getBitstreamWidth() : 0;
        std::vector<BitRef> ob;
        if (!want || !bitList(*other, ec, ob) || ob.size() != want) return false;
        const uint32_t mw = mv.integer().getBitWidth();
        for (uint64_t i = 0; i < want; i++) {
            const uint64_t pos = want - 1 - i;   // bit position of this entry (MSB first)
            logic_t bit = pos < mw ? mv.integer()[static_cast<int32_t>(pos)] : logic_t(false);
            if (bit.value != 0 && bit.value != 1) out.push_back({nullptr, 0, '?'});
            else if ((bit.value == 1) == isAnd) out.push_back(ob[i]);
            else out.push_back({nullptr, 0, bit.value == 1 ? '1' : '0'});
        }
        return true;
    }

    /// The reads that can influence each bit of `e`, most significant bit first. Bitwise operators, conversions, concatenation and
    /// data arms of ?: keep bits apart; every other expression is a leaf whose reads influence all of its bits, so this only ever
    /// removes dependencies that the operator cannot create. A ?: select influences every bit. False when past `budget`.
    bool bitwiseDeps(const Expression& e, EvalContext& ec, std::vector<std::vector<Path>>& out, uint64_t& budget) {
        const Expression* x = &modeExpr(e, ec);
        const uint64_t width = x->type ? x->type->getBitstreamWidth() : 0;
        if (!width || width > budget) return false;
        std::vector<BitRef> exact;
        if (bitList(*x, ec, exact) && exact.size() == width) {
            budget -= width;
            for (auto& bit : exact) {
                out.emplace_back();
                if (bit.sym) out.back().push_back({bit.sym, bit.bit, bit.bit});
            }
            return true;
        }
        const bool integral = x->type->isIntegral();
        const auto recurse = [&](const Expression& operand, std::vector<std::vector<Path>>& bits) {
            return bitwiseDeps(operand, ec, bits, budget) && bits.size() == operand.type->getBitstreamWidth();
        };
        std::vector<std::vector<Path>> a, b;
        if (integral && x->kind == ExpressionKind::UnaryOp) {
            const auto& u = x->as<UnaryExpression>();
            if ((u.op == UnaryOperator::BitwiseNot || u.op == UnaryOperator::Plus) && u.operand().type->getBitstreamWidth() == width) {
                if (!recurse(u.operand(), a)) return false;
                out.insert(out.end(), a.begin(), a.end());
                return true;
            }
        } else if (integral && x->kind == ExpressionKind::BinaryOp) {
            const auto& o = x->as<BinaryExpression>();
            const bool bitwise = o.op == BinaryOperator::BinaryAnd || o.op == BinaryOperator::BinaryOr ||
                                 o.op == BinaryOperator::BinaryXor || o.op == BinaryOperator::BinaryXnor;
            if (bitwise && o.left().type->getBitstreamWidth() == width && o.right().type->getBitstreamWidth() == width) {
                if (!recurse(o.left(), a) || !recurse(o.right(), b)) return false;
                for (uint64_t i = 0; i < width; i++) {
                    a[i].insert(a[i].end(), b[i].begin(), b[i].end());
                    out.push_back(std::move(a[i]));
                }
                return true;
            }
        } else if (integral && x->kind == ExpressionKind::ConditionalOp) {
            const auto& c = x->as<ConditionalExpression>();
            if (c.conditions.size() == 1 && !c.conditions[0].pattern && c.left().type->getBitstreamWidth() == width &&
                c.right().type->getBitstreamWidth() == width) {
                auto select = pathsOf(*c.conditions[0].expr, ec, false);
                if (select.size() * width > budget) return false;
                budget -= select.size() * width;
                if (!recurse(c.left(), a) || !recurse(c.right(), b)) return false;
                for (uint64_t i = 0; i < width; i++) {
                    a[i].insert(a[i].end(), b[i].begin(), b[i].end());
                    a[i].insert(a[i].end(), select.begin(), select.end());
                    out.push_back(std::move(a[i]));
                }
                return true;
            }
        } else if (integral && isBitwisePattern(*x)) {
            for (const auto* element : patternElements(*x)) {
                std::vector<std::vector<Path>> part;
                if (!recurse(*element, part)) return false;
                out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
            return out.size() >= width;
        } else if (x->kind == ExpressionKind::Concatenation) {
            for (const auto* operand : x->as<ConcatenationExpression>().operands()) {
                std::vector<std::vector<Path>> part;
                if (!recurse(*operand, part)) return false;
                out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
            return out.size() >= width;
        } else if (integral && x->kind == ExpressionKind::Conversion) {
            const Expression& operand = x->as<ConversionExpression>().operand();
            const uint64_t from = operand.type->getBitstreamWidth();
            if (operand.type->isIntegral() && from) {
                if (!recurse(operand, a)) return false;
                if (from > width) {   // truncation keeps the low bits
                    out.insert(out.end(), std::make_move_iterator(a.end() - static_cast<std::ptrdiff_t>(width)), std::make_move_iterator(a.end()));
                    return true;
                }
                for (uint64_t i = from; i < width; i++)   // a signed operand's top bit fills the extension
                    out.push_back(operand.type->isSigned() ? a.front() : std::vector<Path>{});
                out.insert(out.end(), std::make_move_iterator(a.begin()), std::make_move_iterator(a.end()));
                return true;
            }
        }
        auto reads = pathsOf(*x, ec, false);
        if (reads.size() * width > budget) return false;
        budget -= reads.size() * width;
        for (uint64_t i = 0; i < width; i++) out.push_back(reads);
        return true;
    }

    bool loopGenerate = false;   // inside the body of a generate-for iteration: its genvar is a constant
    std::vector<std::pair<const ValueSymbol*, ConstantValue>> boundLoops;   // the loop variables of the unrolled loops being walked
    bool hasDynamicSelect(const Expression& e, EvalContext& ec) {
        struct Selects : ASTVisitor<Selects, VisitFlags::AllGood> {
            EvalContext& context;
            bool found = false;
            const std::vector<std::pair<const ValueSymbol*, ConstantValue>>& bound;   // loop variables of an unrolled loop: constants here
            Selects(EvalContext& c, const std::vector<std::pair<const ValueSymbol*, ConstantValue>>& b) : context(c), bound(b) {}
            void handle(const ElementSelectExpression& select) {
                EvalContext tmp(context.astCtx);
                for (const auto& [v, c] : bound) tmp.createLocal(v, c);
                found = found || select.selector().eval(tmp).bad();
                visitDefault(select);
            }
            void handle(const RangeSelectExpression& select) {
                EvalContext tmp(context.astCtx);
                for (const auto& [v, c] : bound) tmp.createLocal(v, c);
                found = found || select.left().eval(tmp).bad() || select.right().eval(tmp).bad();
                visitDefault(select);
            }
        } selects(ec, boundLoops);
        e.visit(selects);
        return selects.found;
    }

    /// For a top-level concatenation, which result bits (most significant first) are plain bit moves. A concatenation is the only
    /// shape that mixes moves and logic in one assignment, as in `{s2, s1} <= {s1, a & b}`.
    std::vector<char> exactBitsOf(const Expression& e, EvalContext& ec, size_t width) {
        std::vector<char> exact(width, 0);
        const Expression* x = &modeExpr(e, ec);
        if (x->kind == ExpressionKind::Conversion && x->as<ConversionExpression>().isImplicit() &&
            x->as<ConversionExpression>().operand().type->getBitstreamWidth() == width)
            x = &modeExpr(x->as<ConversionExpression>().operand(), ec);
        if (x->kind != ExpressionKind::Concatenation) return exact;
        size_t position = 0;
        for (const auto* operand : x->as<ConcatenationExpression>().operands()) {
            const size_t bits = operand->type->getBitstreamWidth();
            std::vector<BitRef> list;
            if (bitList(*operand, ec, list) && list.size() == bits)
                for (size_t i = 0; i < bits && position + i < width; i++) exact[position + i] = list[i].sym != nullptr;
            position += bits;
        }
        return exact;
    }

    /// For each bit of `e` (most significant first): the one source bit it is a copy of, and whether through no inversion (1) or one
    /// (2), through concatenation, replication, inversion (`~x`, `!x` of one bit; two cancel) and static selects. A bit that comes
    /// through logic (or a constant) has no source symbol. Always `width` entries.
    std::vector<std::pair<BitRef, char>> bitMoves(const Expression& e, EvalContext& ec, size_t width) {
        std::vector<std::pair<BitRef, char>> none(width, {BitRef{}, 0});
        const Expression* x = &modeExpr(e, ec);
        if (!x->type || x->type->getBitstreamWidth() != width) return none;
        auto joined = [&](std::vector<std::pair<BitRef, char>> list) { return list.size() == width ? list : none; };
        std::vector<BitRef> plain;
        if (bitList(*x, ec, plain) && plain.size() == width) {
            std::vector<std::pair<BitRef, char>> out;
            for (auto& bit : plain) out.push_back({bit, static_cast<char>(bit.sym ? 1 : 0)});
            return out;
        }
        if (const Expression* operand = inversionOperand(*x, ec)) {
            auto inner = bitMoves(*operand, ec, width);
            for (auto& [bit, kind] : inner) if (kind) kind = static_cast<char>(3 - kind);
            return inner;
        }
        if (x->kind == ExpressionKind::Concatenation) {
            std::vector<std::pair<BitRef, char>> out;
            for (const auto* operand : x->as<ConcatenationExpression>().operands()) {
                const size_t bits = operand->type ? operand->type->getBitstreamWidth() : 0;
                if (!bits) return none;
                auto part = bitMoves(*operand, ec, bits);
                out.insert(out.end(), part.begin(), part.end());
            }
            return joined(std::move(out));
        }
        if (x->kind == ExpressionKind::Replication) {
            const auto& r = x->as<ReplicationExpression>();
            const size_t bits = r.concat().type ? r.concat().type->getBitstreamWidth() : 0;
            if (!bits || width % bits) return none;
            auto part = bitMoves(r.concat(), ec, bits);
            std::vector<std::pair<BitRef, char>> out;
            for (size_t k = 0; k < width / bits; ++k) out.insert(out.end(), part.begin(), part.end());
            return joined(std::move(out));
        }
        return none;
    }

    // ---------------------------------------------------------------- straight-line function calls
    // A call whose callee is a straight-line value function (automatic locals, blocking assignments to constant selects of them,
    // one final return) is expanded bit by bit: each result bit gets the exact set of actual-argument bits it can depend on, and
    // whether it is a plain move of one of them. Anything outside that subset is left to the coarse call model.
    struct CallBit {
        std::vector<Path> reads;   // the argument bits this result bit can depend on (sorted, merged)
        BitRef move;               // sym set: the bit is a copy (kind 1) or the inverse (kind 2) of that one source bit
        char kind = 0;
    };
    using CallBits = std::vector<CallBit>;   // most significant bit first
    using CallArgBits = std::function<bool(const Expression&, CallBits&)>;
    static constexpr uint64_t kCallExpansionWork = uint64_t{1} << 24;
    static constexpr unsigned kCallExpansionDepth = 6;

    struct CallFrame {
        const SubroutineSymbol& sub;
        EvalContext ec;
        std::map<const ValueSymbol*, CallBits> env;   // the bits of each formal and automatic local
        CallBits lvalue;                              // the target of the compound assignment being evaluated
        uint64_t& work;
        unsigned depth;
        bool returned = false;
        CallBits result;
        CallFrame(const SubroutineSymbol& s, uint64_t& w, unsigned d) : sub(s), ec(s), work(w), depth(d) {}
    };
    std::set<const SubroutineSymbol*> expandingCalls;

    static void mergePaths(std::vector<Path>& v) {
        std::sort(v.begin(), v.end());
        size_t n = 0;
        for (size_t i = 0; i < v.size(); i++) {
            if (n && v[n - 1].sym == v[i].sym && v[i].lo <= v[n - 1].hi + 1) v[n - 1].hi = std::max(v[n - 1].hi, v[i].hi);
            else v[n++] = v[i];
        }
        v.resize(n);
    }

    CallBits* callSlot(CallFrame& f, const ValueSymbol& s) {
        if (auto it = f.env.find(&s); it != f.env.end()) return &it->second;
        if (!localTo(s, f.sub) || s.kind != SymbolKind::Variable) return nullptr;
        const auto& var = s.as<VariableSymbol>();
        if (&var != f.sub.returnValVar && var.lifetime != VariableLifetime::Automatic) return nullptr;
        const Type& type = var.getType();
        const uint64_t width = type.isIntegral() ? type.getBitstreamWidth() : 0;
        if (!width || width > kMaxPackedBits) return nullptr;
        return &(f.env[&s] = CallBits(width));
    }

    /// The bit range [lo, hi] a fully static select of a local covers, and the slot it lives in.
    CallBits* callStaticSelect(const Expression& e, CallFrame& f, uint64_t& lo, uint64_t& hi) {
        ValuePath vp(e, f.ec);
        const ValueSymbol* root = vp.rootSymbol();
        if (!root || !localTo(*root, f.sub) || !vp.lsp || !vp.isFullyStatic()) return nullptr;
        CallBits* slot = callSlot(f, *root);
        if (!slot) return nullptr;
        lo = vp.lspBounds.first; hi = vp.lspBounds.second;
        if (lo > hi) std::swap(lo, hi);
        const uint64_t width = e.type && e.type->isIntegral() ? e.type->getBitstreamWidth() : 0;
        if (hi >= slot->size() || hi - lo + 1 != width) return nullptr;
        return slot;
    }

    bool callLeaf(const std::vector<const Expression*>& operands, CallFrame& f, uint64_t width, CallBits& out) {
        std::vector<Path> all;
        for (const auto* operand : operands) {
            CallBits part;
            if (!callExprBits(*operand, f, part)) return false;
            for (const auto& bit : part) all.insert(all.end(), bit.reads.begin(), bit.reads.end());
            mergePaths(all);
        }
        f.work += width * (all.size() + 1);
        if (f.work > kCallExpansionWork) return false;
        CallBit bit;
        bit.reads = std::move(all);
        out.assign(width, bit);
        return true;
    }

    bool callExprBits(const Expression& e, CallFrame& f, CallBits& out) {
        out.clear();
        const uint64_t width = e.type && e.type->isIntegral() ? e.type->getBitstreamWidth() : 0;
        if (!width || width > kMaxPackedBits) return false;
        f.work += width;
        if (f.work > kCallExpansionWork) return false;
        switch (e.kind) {
            case ExpressionKind::NamedValue:
            case ExpressionKind::ElementSelect:
            case ExpressionKind::RangeSelect:
            case ExpressionKind::MemberAccess: {
                ValuePath vp(e, f.ec);
                const ValueSymbol* root = vp.rootSymbol();
                if (root && localTo(*root, f.sub)) {
                    uint64_t lo = 0, hi = 0;
                    const CallBits* slot = callStaticSelect(e, f, lo, hi);
                    if (!slot) return false;
                    const uint64_t total = slot->size();
                    out.assign(slot->begin() + static_cast<std::ptrdiff_t>(total - 1 - hi), slot->begin() + static_cast<std::ptrdiff_t>(total - lo));
                    return true;
                }
                break;
            }
            case ExpressionKind::LValueReference:
                out = f.lvalue;
                return out.size() == width;
            default: break;
        }
        ConstantValue cv;
        if (e.kind == ExpressionKind::Call) { if (const auto* c = e.getConstant()) cv = *c; }
        else { EvalContext tmp(f.sub); cv = e.eval(tmp); }
        if (cv.isInteger() && cv.integer().getBitWidth() == width) {
            out.assign(width, CallBit{});
            for (uint64_t i = 0; i < width; i++) {
                const logic_t bit = cv.integer()[static_cast<int32_t>(width - 1 - i)];
                out[i].move.value = bit.value == 0 ? '0' : bit.value == 1 ? '1' : bit.value == logic_t::z.value ? 'z' : 'x';
            }
            return true;
        }
        const auto flip = [](CallBits& bits) { for (auto& bit : bits) if (bit.kind) bit.kind = static_cast<char>(3 - bit.kind); };
        switch (e.kind) {
            case ExpressionKind::Concatenation:
                for (const auto* operand : e.as<ConcatenationExpression>().operands()) {
                    CallBits part;
                    if (!callExprBits(*operand, f, part)) return false;
                    out.insert(out.end(), part.begin(), part.end());
                }
                return out.size() == width;
            case ExpressionKind::Replication: {
                const auto& r = e.as<ReplicationExpression>();
                EvalContext tmp(f.sub);
                const auto count = r.count().eval(tmp);
                if (!count.isInteger()) return false;
                const auto n = count.integer().as<uint32_t>();
                CallBits part;
                if (!n || !*n || !callExprBits(r.concat(), f, part)) return false;
                for (uint32_t k = 0; k < *n && out.size() <= width; k++) out.insert(out.end(), part.begin(), part.end());
                return out.size() == width;
            }
            case ExpressionKind::Conversion: {
                const Expression& operand = e.as<ConversionExpression>().operand();
                if (!operand.type || !operand.type->isIntegral()) return false;
                CallBits a;
                if (!callExprBits(operand, f, a)) return false;
                if (a.size() >= width) { out.assign(a.end() - static_cast<std::ptrdiff_t>(width), a.end()); return true; }
                out.assign(width - a.size(), operand.type->isSigned() ? a.front() : CallBit{});
                out.insert(out.end(), a.begin(), a.end());
                return true;
            }
            case ExpressionKind::SimpleAssignmentPattern:
            case ExpressionKind::StructuredAssignmentPattern:
                if (!isBitwisePattern(e)) return false;
                for (const auto* element : patternElements(e)) {
                    CallBits part;
                    if (!callExprBits(*element, f, part)) return false;
                    out.insert(out.end(), part.begin(), part.end());
                }
                return out.size() == width;
            case ExpressionKind::UnaryOp: {
                const auto& u = e.as<UnaryExpression>();
                switch (u.op) {
                    case UnaryOperator::Plus: case UnaryOperator::Minus: case UnaryOperator::BitwiseNot:
                    case UnaryOperator::BitwiseAnd: case UnaryOperator::BitwiseOr: case UnaryOperator::BitwiseXor:
                    case UnaryOperator::BitwiseNand: case UnaryOperator::BitwiseNor: case UnaryOperator::BitwiseXnor:
                    case UnaryOperator::LogicalNot: break;
                    default: return false;
                }
                const uint64_t from = u.operand().type && u.operand().type->isIntegral() ? u.operand().type->getBitstreamWidth() : 0;
                const bool keeps = (u.op == UnaryOperator::BitwiseNot && from == width) || (u.op == UnaryOperator::LogicalNot && from == 1 && width == 1);
                if (keeps) {
                    if (!callExprBits(u.operand(), f, out)) return false;
                    flip(out);
                    return out.size() == width;
                }
                if (u.op == UnaryOperator::Plus && from == width) return callExprBits(u.operand(), f, out) && out.size() == width;
                return callLeaf({&u.operand()}, f, width, out);
            }
            case ExpressionKind::BinaryOp: {
                const auto& o = e.as<BinaryExpression>();
                const bool bitwise = o.op == BinaryOperator::BinaryAnd || o.op == BinaryOperator::BinaryOr ||
                                     o.op == BinaryOperator::BinaryXor || o.op == BinaryOperator::BinaryXnor;
                const auto widthOfOperand = [](const Expression& x) { return x.type && x.type->isIntegral() ? x.type->getBitstreamWidth() : uint64_t{0}; };
                if (bitwise && widthOfOperand(o.left()) == width && widthOfOperand(o.right()) == width) {
                    CallBits a, b2;
                    if (!callExprBits(o.left(), f, a) || !callExprBits(o.right(), f, b2) || a.size() != width || b2.size() != width) return false;
                    out.assign(width, CallBit{});
                    for (uint64_t i = 0; i < width; i++) {
                        out[i].reads = std::move(a[i].reads);
                        out[i].reads.insert(out[i].reads.end(), b2[i].reads.begin(), b2[i].reads.end());
                        mergePaths(out[i].reads);
                    }
                    return true;
                }
                return callLeaf({&o.left(), &o.right()}, f, width, out);
            }
            case ExpressionKind::ConditionalOp: {
                const auto& c = e.as<ConditionalExpression>();
                if (c.conditions.size() != 1 || c.conditions[0].pattern) return false;
                return callLeaf({c.conditions[0].expr, &c.left(), &c.right()}, f, width, out);
            }
            case ExpressionKind::Call: {
                const auto& call = e.as<CallExpression>();
                if (call.hasOutputArgs()) return false;
                if (call.isSystemCall()) {
                    static const std::set<std::string_view> pure = {"$clog2", "$signed", "$unsigned", "$countones", "$onehot", "$onehot0",
                                                                     "$isunknown", "$bits", "$size", "$high", "$low", "$left", "$right",
                                                                     "$increment", "$dimensions"};
                    if (!pure.count(call.getSubroutineName())) return false;
                    std::vector<const Expression*> args(call.arguments().begin(), call.arguments().end());
                    return callLeaf(args, f, width, out);
                }
                const CallArgBits nested = [&](const Expression& actual, CallBits& bits) { return callExprBits(actual, f, bits); };
                if (expandCall(call, nested, out, f.work, f.depth) && out.size() == width) return true;
                // Not straight-line, but a pure function of its arguments: every result bit depends on all of them.
                const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
                if (!sub || call.thisClass()) return false;
                const auto& summary = summarize(*sub);
                if (!summary.unknown.empty() || !summary.reads.empty() || !summary.writes.empty() || summary.unboundedWrites) return false;
                std::vector<const Expression*> args(call.arguments().begin(), call.arguments().end());
                return callLeaf(args, f, width, out);
            }
            default: return false;
        }
    }

    bool callAssign(const AssignmentExpression& a, CallFrame& f) {
        if (a.timingControl || a.isNonBlocking()) return false;
        uint64_t lo = 0, hi = 0;
        CallBits* slot = callStaticSelect(a.left(), f, lo, hi);
        if (!slot) return false;
        const uint64_t width = hi - lo + 1;
        const uint64_t total = slot->size();
        f.lvalue.assign(slot->begin() + static_cast<std::ptrdiff_t>(total - 1 - hi), slot->begin() + static_cast<std::ptrdiff_t>(total - lo));
        CallBits value;
        if (!callExprBits(a.right(), f, value) || value.size() != width) return false;
        std::copy(value.begin(), value.end(), slot->begin() + static_cast<std::ptrdiff_t>(total - 1 - hi));
        return true;
    }

    bool callStatement(const Statement& s, CallFrame& f) {
        if (f.returned) return false;
        if (++f.work > kCallExpansionWork) return false;
        switch (s.kind) {
            case StatementKind::Empty: return true;
            case StatementKind::List:
                for (const auto* child : s.as<StatementList>().list)
                    if (!callStatement(*child, f)) return false;
                return true;
            case StatementKind::Block:
                return s.as<BlockStatement>().blockKind == StatementBlockKind::Sequential && callStatement(s.as<BlockStatement>().body, f);
            case StatementKind::VariableDeclaration: {
                const auto& var = s.as<VariableDeclStatement>().symbol;
                f.env.erase(&var);
                CallBits* slot = callSlot(f, var);
                if (!slot) return false;
                if (const auto* init = var.getInitializer()) {
                    CallBits value;
                    if (!callExprBits(*init, f, value) || value.size() != slot->size()) return false;
                    f.env[&var] = std::move(value);
                }
                return true;
            }
            case StatementKind::ExpressionStatement: {
                const auto& expr = s.as<ExpressionStatement>().expr;
                return expr.kind == ExpressionKind::Assignment && callAssign(expr.as<AssignmentExpression>(), f);
            }
            case StatementKind::Return: {
                const auto* expr = s.as<ReturnStatement>().expr;
                if (!expr || !callExprBits(*expr, f, f.result)) return false;
                f.returned = true;
                return true;
            }
            default: return false;
        }
    }

    /// The per-bit result of `call`, with `argBits` giving the bits of each actual argument. False outside the supported subset.
    bool expandCall(const CallExpression& call, const CallArgBits& argBits, CallBits& out, uint64_t& work, unsigned depth) {
        if (call.isSystemCall() || call.thisClass() || depth >= kCallExpansionDepth || call.hasOutputArgs()) return false;
        const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
        if (!sub || sub->subroutineKind != SubroutineKind::Function || sub->defaultLifetime != VariableLifetime::Automatic ||
            sub->flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) || sub->isVirtual() || sub->hasOutputArgs() ||
            !sub->returnValVar || expandingCalls.count(sub))
            return false;
        const Type& returnType = sub->getReturnType();
        const uint64_t resultWidth = returnType.isIntegral() ? returnType.getBitstreamWidth() : 0;
        auto formals = sub->getArguments();
        auto actuals = call.arguments();
        if (!resultWidth || resultWidth > kMaxPackedBits || formals.size() != actuals.size()) return false;
        const auto& summary = summarize(*sub);
        if (!summary.unknown.empty() || !summary.reads.empty() || !summary.writes.empty() || summary.unboundedWrites) return false;
        for (const auto* formal : formals)
            if (formal->direction != ArgumentDirection::In || !formal->getType().isIntegral()) return false;
        expandingCalls.insert(sub);
        struct Done { Builder& b; const SubroutineSymbol* s; ~Done() { b.expandingCalls.erase(s); } } done{*this, sub};
        CallFrame f(*sub, work, depth + 1);
        for (size_t i = 0; i < formals.size(); i++) {
            CallBits bits;
            const uint64_t width = formals[i]->getType().getBitstreamWidth();
            if (!width || !argBits(*actuals[i], bits) || bits.size() != width) return false;
            f.env[formals[i]] = std::move(bits);
        }
        if (!callStatement(sub->getBody(), f)) return false;
        if (f.returned) out = std::move(f.result);
        else if (const CallBits* slot = callSlot(f, *sub->returnValVar)) out = *slot;
        else return false;
        return out.size() == resultWidth;
    }

    /// The per-bit result when `rhs` is, up to same-width conversions, one call of a straight-line value function.
    bool expandWholeCall(const Expression& rhs, EvalContext& ec, CallBits& out) {
        const Expression* x = &modeExpr(rhs, ec);
        while (x->kind == ExpressionKind::Conversion) {
            const Expression& operand = x->as<ConversionExpression>().operand();
            if (!x->type || !operand.type || !x->type->isIntegral() || !operand.type->isIntegral() ||
                x->type->getBitstreamWidth() != operand.type->getBitstreamWidth())
                return false;
            x = &operand;
        }
        if (x->kind != ExpressionKind::Call || x->as<CallExpression>().isSystemCall()) return false;
        uint64_t work = 0;
        const CallArgBits caller = [&](const Expression& actual, CallBits& bits) {
            const uint64_t width = actual.type && actual.type->isIntegral() ? actual.type->getBitstreamWidth() : 0;
            if (!width || width > kMaxPackedBits) return false;
            std::vector<std::vector<Path>> deps;
            uint64_t budget = kMaxEdgesPerAssign;
            if (!bitwiseDeps(actual, ec, deps, budget) || deps.size() != width) return false;
            auto moves = bitMoves(actual, ec, width);
            bits.assign(width, CallBit{});
            for (uint64_t i = 0; i < width; i++) {
                bits[i].reads = std::move(deps[i]);
                bits[i].move = moves[i].first;
                bits[i].kind = moves[i].second;
                if (bits[i].kind && bits[i].move.sym) bits[i].reads = {Path{bits[i].move.sym, bits[i].move.bit, bits[i].move.bit}};
                else bits[i].kind = 0;
                mergePaths(bits[i].reads);
            }
            return true;
        };
        return expandCall(x->as<CallExpression>(), caller, out, work, 0);
    }

    BoolFactPtr boolFact(BoolFact::Kind kind, std::vector<BoolFactPtr> args = {}, NodeId node = kNoNode,
                         bool value = false, bool opaque = false) {
        auto result = std::make_shared<BoolFact>();
        result->kind = kind; result->args = std::move(args); result->node = node;
        result->value = value; result->opaque = opaque;
        return result;
    }
    BoolFactPtr unknownBoolean(const Expression& expr, EvalContext& ec, const InstanceSymbol* ctx) {
        std::vector<BoolFactPtr> refs;
        for (const auto& path : pathsOf(expr, ec, false))
            for (auto node : nodesFor(ctx, path)) refs.push_back(boolFact(BoolFact::Kind::Node, {}, node));
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            Builder& builder;
            const InstanceSymbol* ctx;
            bool opaque = false;
            Calls(Builder& builder, const InstanceSymbol* ctx) : builder(builder), ctx(ctx) {}
            void handle(const CallExpression& call) {
                if (call.isSystemCall() || builder.callIssue(call, ctx) != UINT32_MAX) opaque = true;
                visitDefault(call);
            }
        } calls(*this, ctx);
        expr.visit(calls);
        return boolFact(BoolFact::Kind::Unknown, std::move(refs), kNoNode, false, calls.opaque);
    }
    BoolFactPtr booleanOf(const Expression& expr, EvalContext& ec, const InstanceSymbol* ctx) {
        if (design.hasCaseMode()) {
            int v = modeValue(expr, ec);
            if (v >= 0) return boolFact(BoolFact::Kind::Constant, {}, kNoNode, v);
        }
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression&) { found = true; }
        } calls;
        expr.visit(calls);
        auto value = calls.found ? ConstantValue() : expr.eval(ec);
        if (value.isInteger() && !value.hasUnknown())
            return boolFact(BoolFact::Kind::Constant, {}, kNoNode, value.isTrue());
        if (expr.kind == ExpressionKind::Conversion) {
            const auto& operand = expr.as<ConversionExpression>().operand();
            if (expr.type->getBitstreamWidth() == operand.type->getBitstreamWidth()) return booleanOf(operand, ec, ctx);
        }
        if (expr.kind == ExpressionKind::NamedValue || expr.kind == ExpressionKind::HierarchicalValue ||
            expr.kind == ExpressionKind::ElementSelect) {
            auto paths = pathsOf(expr, ec, false);
            if (paths.size() == 1 && pathWidth(paths[0]) == 1) {
                auto nodes = nodesFor(ctx, paths[0]);
                if (nodes.size() == 1) return boolFact(BoolFact::Kind::Node, {}, nodes[0]);
            }
        }
        if (expr.kind == ExpressionKind::UnaryOp) {
            const auto& unary = expr.as<UnaryExpression>();
            if (unary.op == UnaryOperator::LogicalNot ||
                (unary.op == UnaryOperator::BitwiseNot && unary.operand().type->getBitstreamWidth() == 1))
                return boolFact(BoolFact::Kind::Not, {booleanOf(unary.operand(), ec, ctx)});
        }
        if (expr.kind == ExpressionKind::BinaryOp) {
            const auto& op = expr.as<BinaryExpression>();
            auto zeroOrOne = [&](const Expression& operand) {
                auto constant = operand.eval(ec);
                if (!constant.isInteger() || constant.integer().hasUnknown()) return false;
                const auto& integer = constant.integer();
                for (uint32_t bit = 1; bit < integer.getBitWidth(); bit++)
                    if (integer[static_cast<int32_t>(bit)].value != 0) return false;
                return true;
            };
            bool scalar = (op.left().type->getBitstreamWidth() == 1 && op.right().type->getBitstreamWidth() == 1) ||
                          (op.left().type->getBitstreamWidth() == 1 && zeroOrOne(op.right())) ||
                          (op.right().type->getBitstreamWidth() == 1 && zeroOrOne(op.left()));
            if (op.op == BinaryOperator::LogicalAnd || (scalar && op.op == BinaryOperator::BinaryAnd))
                return boolFact(BoolFact::Kind::And, {booleanOf(op.left(), ec, ctx), booleanOf(op.right(), ec, ctx)});
            if (op.op == BinaryOperator::LogicalOr || (scalar && op.op == BinaryOperator::BinaryOr))
                return boolFact(BoolFact::Kind::Or, {booleanOf(op.left(), ec, ctx), booleanOf(op.right(), ec, ctx)});
            if (scalar && (op.op == BinaryOperator::Equality || op.op == BinaryOperator::Inequality)) {
                auto left = booleanOf(op.left(), ec, ctx);
                auto right = booleanOf(op.right(), ec, ctx);
                // Compare exact scalar operands only. Keep opaque or more
                // complex operands under the existing uncertainty policy.
                auto exact = [](const BoolFactPtr& fact) {
                    return fact->kind == BoolFact::Kind::Node || fact->kind == BoolFact::Kind::Constant;
                };
                if (exact(left) && exact(right) && !(left->kind == BoolFact::Kind::Node && right->kind == BoolFact::Kind::Node &&
                                                     left->node == right->node)) {
                    auto equal = boolFact(BoolFact::Kind::Equal, {left, right});
                    return op.op == BinaryOperator::Equality ? equal : boolFact(BoolFact::Kind::Not, {equal});
                }
            }
        }
        return unknownBoolean(expr, ec, ctx);
    }

    // Bind formal condition reads to actual arguments. Locals and nested/opaque
    // calls retain an unresolved argument dependency rather than implying safety.
    void calledConditions(const CallExpression& call, EvalContext& ec, Assign& assignment) {
        if (call.isSystemCall()) return;
        const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
        std::vector<Path> actuals;
        for (auto* arg : call.arguments()) {
            auto paths = pathsOf(*arg, ec, false);
            actuals.insert(actuals.end(), paths.begin(), paths.end());
        }
        const auto& summary = summaryFor(call);
        actuals.insert(actuals.end(), summary.reads.begin(), summary.reads.end());
        const auto location = toLoc(*sm, call.sourceRange.start());
        const auto name = std::string(call.getSubroutineName());
        if (!sub || !summary.unknown.empty())
            assignment.calledConditions.push_back({location, actuals, name, true});
        if (!sub) return;
        struct Conditions : ASTVisitor<Conditions, VisitFlags::AllGood> {
            Builder& b; EvalContext& ec; Assign& assignment;
            const CallExpression& call; const SubroutineSymbol& sub;
            const std::vector<Path>& actuals;
            Conditions(Builder& b, EvalContext& ec, Assign& a, const CallExpression& c,
                       const SubroutineSymbol& s, const std::vector<Path>& actuals)
                : b(b), ec(ec), assignment(a), call(c), sub(s), actuals(actuals) {}
            void note(const Expression& expression) {
                std::vector<Path> reads;
                bool unresolved = false;
                std::vector<Path> paths;
                ValuePath::visitPaths(expression, ec, [&](const ValuePath& path) {
                    if (const auto* symbol = path.rootSymbol())
                        paths.push_back({symbol, 0, widthOf(*symbol) ? widthOf(*symbol) - 1 : 0});
                });
                for (const auto& path : paths) {
                    auto args = sub.getArguments();
                    auto it = std::find(args.begin(), args.end(), path.sym);
                    if (it != args.end()) {
                        size_t index = size_t(it - args.begin());
                        if (index < call.arguments().size()) {
                            auto bound = b.pathsOf(*call.arguments()[index], ec, false);
                            reads.insert(reads.end(), bound.begin(), bound.end());
                        }
                        else unresolved = true;
                    }
                    else if (localTo(*path.sym, sub)) unresolved = true;
                    else reads.push_back(path);
                }
                if (unresolved) reads.insert(reads.end(), actuals.begin(), actuals.end());
                assignment.calledConditions.push_back({toLoc(*b.sm, call.sourceRange.start()), reads,
                                                       std::string(call.getSubroutineName()), unresolved});
            }
            void handle(const ConditionalExpression& e) {
                for (const auto& c : e.conditions) note(*c.expr);
                visitDefault(e);
            }
            void handle(const ConditionalStatement& s) {
                for (const auto& c : s.conditions) note(*c.expr);
                visitDefault(s);
            }
            void handle(const CaseStatement& s) { note(s.expr); visitDefault(s); }
            void writeIndices(const Expression& target) {
                switch (target.kind) {
                    case ExpressionKind::ElementSelect: {
                        const auto& select = target.as<ElementSelectExpression>();
                        note(select.selector());
                        writeIndices(select.value());
                        break;
                    }
                    case ExpressionKind::RangeSelect: {
                        const auto& select = target.as<RangeSelectExpression>();
                        note(select.left());
                        note(select.right());
                        writeIndices(select.value());
                        break;
                    }
                    case ExpressionKind::MemberAccess:
                        writeIndices(target.as<MemberAccessExpression>().value());
                        break;
                    case ExpressionKind::Concatenation:
                        for (const auto* operand : target.as<ConcatenationExpression>().operands()) writeIndices(*operand);
                        break;
                    default: break;
                }
            }
            void handle(const AssignmentExpression& expression) {
                writeIndices(expression.left());
                visitDefault(expression);
            }
            void handle(const CallExpression& nested) {
                if (!nested.isSystemCall())
                    assignment.calledConditions.push_back({toLoc(*b.sm, call.sourceRange.start()), actuals,
                                                           std::string(call.getSubroutineName()), true});
                visitDefault(nested);
            }
        } visitor(*this, ec, assignment, call, *sub, actuals);
        sub->getBody().visit(visitor);
    }

    static constexpr size_t kMaxQualifierArmsPerBit = 16;
    static constexpr unsigned kMaxQualifierArmDepth = 8;
    std::unordered_map<NodeId, uint32_t> combDriverProc;

    std::vector<NodeId> nodesOfExpr(const Expression& expr, EvalContext& ec, const InstanceSymbol* ctx) {
        std::vector<NodeId> nodes;
        for (const auto& path : pathsOf(expr, ec, false))
            for (auto n : nodesFor(ctx, path)) nodes.push_back(n);
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        return nodes;
    }

    bool hasDecomposableMux(const Expression& expr, EvalContext& ec) {
        const Expression* x = &modeExpr(expr, ec);
        while (x->kind == ExpressionKind::Conversion) {
            const auto& conv = x->as<ConversionExpression>();
            if (!x->type || !conv.operand().type ||
                x->type->getBitstreamWidth() != conv.operand().type->getBitstreamWidth()) break;
            x = &modeExpr(conv.operand(), ec);
        }
        if (x->kind == ExpressionKind::ConditionalOp) return true;
        if (x->kind == ExpressionKind::Concatenation) {
            for (const auto* op : x->as<ConcatenationExpression>().operands())
                if (hasDecomposableMux(*op, ec)) return true;
        }
        if (isBitwisePattern(*x)) {
            for (const auto* el : patternElements(*x))
                if (hasDecomposableMux(*el, ec)) return true;
        }
        return false;
    }

    std::vector<std::vector<CombQualifierArm>> decomposeExprArms(const Expression& expr, EvalContext& ec,
                                                                 const InstanceSymbol* ctx, size_t width,
                                                                 unsigned depth = 0) {
        std::vector<std::vector<CombQualifierArm>> out(width);
        if (!width) return out;
        const Expression* x = &modeExpr(expr, ec);
        while (x->kind == ExpressionKind::Conversion) {
            const auto& conv = x->as<ConversionExpression>();
            if (!x->type || !conv.operand().type ||
                x->type->getBitstreamWidth() != conv.operand().type->getBitstreamWidth()) break;
            x = &modeExpr(conv.operand(), ec);
        }
        if (depth < kMaxQualifierArmDepth) {
            if (x->kind == ExpressionKind::ConditionalOp) {
                const auto& c = x->as<ConditionalExpression>();
                if (c.conditions.size() == 1 && !c.conditions[0].pattern &&
                    c.left().type && c.right().type &&
                    c.left().type->getBitstreamWidth() == width &&
                    c.right().type->getBitstreamWidth() == width) {
                    const Expression& condExpr = *c.conditions[0].expr;
                    GuardCondition trueGuard{booleanOf(condExpr, ec, ctx), nodesOfExpr(condExpr, ec, ctx)};
                    GuardCondition falseGuard{boolFact(BoolFact::Kind::Not, {trueGuard.predicate}), trueGuard.dependencies};
                    const bool condApprox = hasDynamicSelect(condExpr, ec) || !callsIn(condExpr).empty();
                    const bool condUncertain = !callIssues(condExpr, ctx).empty();
                    auto leftBits = decomposeExprArms(c.left(), ec, ctx, width, depth + 1);
                    auto rightBits = decomposeExprArms(c.right(), ec, ctx, width, depth + 1);
                    bool fits = true;
                    for (size_t i = 0; i < width; ++i)
                        if (leftBits[i].size() + rightBits[i].size() > kMaxQualifierArmsPerBit) { fits = false; break; }
                    if (fits) {
                        auto attach = [&](CombQualifierArm arm, const GuardCondition& guard) {
                            arm.guards.insert(arm.guards.begin(), guard);
                            auto ctrl = std::make_shared<std::vector<NodeId>>(guard.dependencies);
                            if (arm.controlDependencies)
                                ctrl->insert(ctrl->end(), arm.controlDependencies->begin(), arm.controlDependencies->end());
                            std::sort(ctrl->begin(), ctrl->end());
                            ctrl->erase(std::unique(ctrl->begin(), ctrl->end()), ctrl->end());
                            arm.controlDependencies = std::move(ctrl);
                            arm.controlApproximate |= condApprox;
                            arm.uncertainCall |= condUncertain;
                            return arm;
                        };
                        for (size_t i = 0; i < width; ++i) {
                            for (auto& arm : leftBits[i]) out[i].push_back(attach(std::move(arm), trueGuard));
                            for (auto& arm : rightBits[i]) out[i].push_back(attach(std::move(arm), falseGuard));
                        }
                        return out;
                    }
                }
            } else if (x->kind == ExpressionKind::Concatenation || isBitwisePattern(*x)) {
                std::vector<const Expression*> parts;
                if (x->kind == ExpressionKind::Concatenation) {
                    for (const auto* op : x->as<ConcatenationExpression>().operands()) parts.push_back(op);
                } else {
                    for (const auto* el : patternElements(*x)) parts.push_back(el);
                }
                size_t totalWidth = 0;
                bool widthsOk = !parts.empty();
                for (const auto* part : parts) {
                    const size_t pw = part->type ? part->type->getBitstreamWidth() : 0;
                    if (!pw) { widthsOk = false; break; }
                    totalWidth += pw;
                }
                if (widthsOk && totalWidth == width) {
                    size_t idx = 0;
                    for (const auto* part : parts) {
                        const size_t pw = part->type->getBitstreamWidth();
                        auto partArms = decomposeExprArms(*part, ec, ctx, pw, depth + 1);
                        for (size_t j = 0; j < pw; ++j) out[idx++] = std::move(partArms[j]);
                    }
                    return out;
                }
            }
        }
        // Leaf expression: extract per-bit dependencies once.
        const bool leafApprox = hasDynamicSelect(*x, ec) || !callsIn(*x).empty();
        const bool leafUncertain = !callIssues(*x, ctx).empty();
        const auto emptyDeps = std::make_shared<const std::vector<NodeId>>();
        std::vector<BitRef> exactBits, invBits;
        std::vector<std::vector<Path>> bitDeps;
        uint64_t budget = kMaxEdgesPerAssign;
        if (bitList(*x, ec, exactBits) && exactBits.size() == width) {
            for (size_t i = 0; i < width; ++i) {
                auto deps = exactBits[i].sym ? std::make_shared<const std::vector<NodeId>>(1, nodeForBit(ctx, exactBits[i])) : emptyDeps;
                out[i].push_back({{}, std::move(deps), emptyDeps, leafApprox, false, leafUncertain, false});
            }
            return out;
        }
        if (invertedBitMove(*x, ec, invBits) && invBits.size() == width) {
            for (size_t i = 0; i < width; ++i) {
                auto deps = std::make_shared<const std::vector<NodeId>>(1, nodeForBit(ctx, invBits[i]));
                out[i].push_back({{}, std::move(deps), emptyDeps, leafApprox, false, leafUncertain, false});
            }
            return out;
        }
        if (bitwiseDeps(*x, ec, bitDeps, budget) && bitDeps.size() == width) {
            for (size_t i = 0; i < width; ++i) {
                auto deps = std::make_shared<std::vector<NodeId>>();
                for (const auto& p : bitDeps[i])
                    for (auto n : nodesFor(ctx, p)) deps->push_back(n);
                std::sort(deps->begin(), deps->end());
                deps->erase(std::unique(deps->begin(), deps->end()), deps->end());
                out[i].push_back({{}, std::move(deps), emptyDeps, leafApprox, false, leafUncertain, false});
            }
            return out;
        }
        auto sharedDeps = std::make_shared<const std::vector<NodeId>>(nodesOfExpr(*x, ec, ctx));
        for (size_t i = 0; i < width; ++i)
            out[i].push_back({{}, sharedDeps, emptyDeps, leafApprox, false, leafUncertain, false});
        return out;
    }

    void noteCombTarget(NodeId target, uint32_t proc) {
        if (target == kNoNode || target >= m.nodes.size() || !m.nodes[target].sym) return;
        auto [it, inserted] = combDriverProc.emplace(target, proc);
        if (!inserted && it->second != proc) {
            auto cit = m.combQualifierDrivers.find(target);
            if (cit != m.combQualifierDrivers.end()) {
                cit->second.supported = false;
                cit->second.multipleDrivers = true;
                cit->second.arms.clear();
            }
        }
    }

    void recordCombDriver(NodeId target, uint32_t proc, CombQualifierSummary summary) {
        auto [it, inserted] = combDriverProc.emplace(target, proc);
        if (!inserted && it->second != proc) {
            auto& existing = m.combQualifierDrivers[target];
            existing.supported = false;
            existing.multipleDrivers = true;
            existing.arms.clear();
            return;
        }
        summary.proc = proc;
        m.combQualifierDrivers[target] = std::move(summary);
    }

    bool alwaysFalseExpr(const Expression& expr, EvalContext& ec) {
        struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
            bool found = false;
            void handle(const CallExpression&) { found = true; }
        } calls;
        expr.visit(calls);
        if (calls.found) return false;
        if (design.hasCaseMode() && modeValue(expr, ec) == 0) return true;
        auto value = expr.eval(ec);
        if (!value.bad() && value.isInteger() && !value.hasUnknown()) return value.isFalse();
        if (expr.kind == ExpressionKind::Conversion)
            return alwaysFalseExpr(expr.as<ConversionExpression>().operand(), ec);
        if (expr.kind == ExpressionKind::BinaryOp) {
            const auto& op = expr.as<BinaryExpression>();
            if (op.op == BinaryOperator::LogicalAnd)
                return alwaysFalseExpr(op.left(), ec) || alwaysFalseExpr(op.right(), ec);
        }
        return false;
    }

    using LoopBinding = std::vector<std::pair<const ValueSymbol*, ConstantValue>>;
    static void lvalueRootsOf(const Expression& e, std::vector<const Symbol*>& out) {
        switch (e.kind) {
            case ExpressionKind::Concatenation:
                for (auto* op : e.as<ConcatenationExpression>().operands()) lvalueRootsOf(*op, out);
                return;
            case ExpressionKind::ElementSelect: lvalueRootsOf(e.as<ElementSelectExpression>().value(), out); return;
            case ExpressionKind::RangeSelect: lvalueRootsOf(e.as<RangeSelectExpression>().value(), out); return;
            case ExpressionKind::MemberAccess: lvalueRootsOf(e.as<MemberAccessExpression>().value(), out); return;
            case ExpressionKind::Conversion: lvalueRootsOf(e.as<ConversionExpression>().operand(), out); return;
            default: if (const auto* sym = e.getSymbolReference()) out.push_back(sym);
        }
    }
    bool unrollForLoop(const ForLoopStatement& s, EvalContext& ec, std::vector<LoopBinding>& iterations) {
        static constexpr size_t kMaxUnrolled = 256;
        if (!s.stopExpr || s.steps.empty()) return false;
        EvalContext tmp(ec.astCtx);
        for (const auto& [v, c] : boundLoops) tmp.createLocal(v, c);
        std::vector<const ValueSymbol*> vars;
        for (const auto* v : s.loopVars) {
            const auto* init = v->getInitializer();
            if (!init) return false;
            auto value = init->eval(tmp);
            if (value.bad()) return false;
            tmp.createLocal(v, std::move(value));
            vars.push_back(v);
        }
        for (auto* i : s.initializers) {
            const auto* a = i->as_if<AssignmentExpression>();
            const auto* n = a && !a->isCompound() ? a->left().as_if<NamedValueExpression>() : nullptr;
            const auto* v = n ? n->symbol.as_if<VariableSymbol>() : nullptr;
            if (!v) return false;
            if (!tmp.findLocal(v)) { tmp.createLocal(v, ConstantValue()); vars.push_back(v); }
            if (i->eval(tmp).bad()) return false;
        }
        if (vars.empty()) return false;
        struct Body : ASTVisitor<Body, VisitFlags::AllGood> {
            std::set<const Symbol*> vars;
            bool ok = true;
            void handle(const BreakStatement&) { ok = false; }
            void handle(const ContinueStatement&) { ok = false; }
            void handle(const ReturnStatement&) { ok = false; }
            void handle(const DisableStatement&) { ok = false; }
            void handle(const DisableForkStatement&) { ok = false; }
            void handle(const TimedStatement&) { ok = false; }
            void handle(const WaitStatement&) { ok = false; }
            void handle(const WaitForkStatement&) { ok = false; }
            void handle(const WaitOrderStatement&) { ok = false; }
            void handle(const ConcurrentAssertionStatement& s) {
                if (s.assertionKind == AssertionKind::Expect) ok = false;
                visitDefault(s);
            }
            void handle(const CallExpression& c) { if (!c.isSystemCall()) ok = false; visitDefault(c); }
            void written(const Expression& e) {
                std::vector<const Symbol*> roots;
                lvalueRootsOf(e, roots);
                for (const auto* r : roots) ok = ok && !vars.count(r);
            }
            void handle(const AssignmentExpression& a) { written(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
                    u.op == UnaryOperator::Postdecrement) written(u.operand());
                visitDefault(u);
            }
        } body;
        body.vars.insert(vars.begin(), vars.end());
        s.body.visit(body);
        if (!body.ok) return false;
        for (;;) {
            if (iterations.size() >= kMaxUnrolled) return false;
            const auto go = s.stopExpr->eval(tmp);
            if (go.bad()) return false;
            if (!go.isTrue()) return true;
            LoopBinding now = boundLoops;
            for (const auto* v : vars) {
                const auto* local = tmp.findLocal(v);
                if (!local || local->bad()) return false;
                now.push_back({v, *local});
            }
            iterations.push_back(std::move(now));
            for (auto* st : s.steps)
                if (st->eval(tmp).bad()) return false;
        }
    }
    bool unrollForeachLoop(const ForeachLoopStatement& s, EvalContext& /*ec*/, std::vector<LoopBinding>& iterations) {
        static constexpr size_t kMaxUnrolled = 256;
        std::vector<const ForeachLoopStatement::LoopDim*> dims;
        std::vector<const ValueSymbol*> vars;
        uint64_t total = 1;
        for (const auto& d : s.loopDims) {
            if (!d.loopVar) continue;
            if (!d.range) return false;
            dims.push_back(&d);
            vars.push_back(d.loopVar);
            const uint64_t w = d.range->width();
            if (w == 0) return true;
            if (w > kMaxUnrolled || total * w > kMaxUnrolled) return false;
            total *= w;
        }
        if (dims.empty()) return false;
        struct Body : ASTVisitor<Body, VisitFlags::AllGood> {
            std::set<const Symbol*> vars;
            bool ok = true;
            void handle(const BreakStatement&) { ok = false; }
            void handle(const ContinueStatement&) { ok = false; }
            void handle(const ReturnStatement&) { ok = false; }
            void handle(const DisableStatement&) { ok = false; }
            void handle(const DisableForkStatement&) { ok = false; }
            void handle(const TimedStatement&) { ok = false; }
            void handle(const WaitStatement&) { ok = false; }
            void handle(const WaitForkStatement&) { ok = false; }
            void handle(const WaitOrderStatement&) { ok = false; }
            void handle(const ConcurrentAssertionStatement& st) {
                if (st.assertionKind == AssertionKind::Expect) ok = false;
                visitDefault(st);
            }
            void handle(const CallExpression& c) { if (!c.isSystemCall()) ok = false; visitDefault(c); }
            void written(const Expression& e) {
                std::vector<const Symbol*> roots;
                lvalueRootsOf(e, roots);
                for (const auto* r : roots) ok = ok && !vars.count(r);
            }
            void handle(const AssignmentExpression& a) { written(a.left()); visitDefault(a); }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
                    u.op == UnaryOperator::Postdecrement) written(u.operand());
                visitDefault(u);
            }
        } body;
        body.vars.insert(vars.begin(), vars.end());
        s.body.visit(body);
        if (!body.ok) return false;
        LoopBinding cur = boundLoops;
        std::function<void(size_t)> emit = [&](size_t k) {
            if (k == dims.size()) {
                iterations.push_back(cur);
                return;
            }
            const auto& dim = *dims[k];
            const Type& t = dim.loopVar->getType();
            const int32_t step = dim.range->left <= dim.range->right ? 1 : -1;
            for (int32_t i = dim.range->left;; i += step) {
                cur.push_back({dim.loopVar, ConstantValue(SVInt(static_cast<bitwidth_t>(t.getBitWidth()),
                                                                static_cast<uint64_t>(static_cast<int64_t>(i)), t.isSigned()))});
                emit(k + 1);
                cur.pop_back();
                if (i == dim.range->right) break;
            }
        };
        emit(0);
        return true;
    }

    // ---------------------------------------------------------------- statement walking
    struct ConditionalReads : ASTVisitor<ConditionalReads, VisitFlags::AllGood> {
        Builder& b;
        EvalContext& ec;
        Assign& assignment;
        ConditionalReads(Builder& b, EvalContext& ec, Assign& assignment) : b(b), ec(ec), assignment(assignment) {}
        void handle(const CallExpression& call) {
            b.calledConditions(call, ec, assignment);
            visitDefault(call);
        }
        void handle(const ConditionalExpression& expression) {
            for (const auto& condition : expression.conditions)
                assignment.conditions.push_back({toLoc(*b.sm, condition.expr->sourceRange.start()),
                                                  b.pathsOf(*condition.expr, ec, false)});
            visitDefault(expression);
        }
    };

    struct StmtVisitor : ASTVisitor<StmtVisitor, VisitFlags::AllGood> {
        StmtVisitor(Builder& b, EvalContext& ec, std::vector<Assign>& out, const InstanceSymbol* ctx)
            : b(b), ec(ec), out(out), ctx(ctx) {}
        Builder& b;
        EvalContext& ec;
        std::vector<Assign>& out;
        const InstanceSymbol* ctx;
        bool modelSteps = false;   // ++ and -- are writes that read their operand (set for combinational procedures)
        struct Ctl {
            SourceLocation loc;
            std::vector<Path> paths;
            OpMap ops;
            Branch br = Branch::None;
            BoolFactPtr predicate;
            std::vector<uint32_t> uncertainCalls;
        };
        std::vector<Ctl> control;

        void addReads(const Expression& e, std::vector<Path>& dst) {
            for (auto& p : b.pathsOf(e, ec, false)) dst.push_back(p);
        }
        void pushControl(const Expression& e, Branch br = Branch::None, bool boolean = true) {
            for (const auto* call : b.callsIn(e)) callEffects(*call);
            control.emplace_back();
            control.back().loc = toLoc(*b.sm, e.sourceRange.start());
            addReads(e, control.back().paths);
            classify(e, Op::None, 0, control.back().ops);
            control.back().br = br;
            control.back().uncertainCalls = b.callIssues(e, ctx);
            control.back().predicate = boolean ? b.booleanOf(e, ec, ctx) : b.unknownBoolean(e, ec, ctx);
        }

        bool alwaysFalse(const Expression& expr) { return b.alwaysFalseExpr(expr, ec); }
        void handle(const ConditionalStatement& s) {
            if (b.design.hasCaseMode() && s.conditions.size() == 1 && !s.conditions[0].pattern) {
                int v = b.modeValue(*s.conditions[0].expr, ec);
                if (v >= 0) {
                    ++b.m.casePrunedBranches;
                    if (v) s.ifTrue.visit(*this);
                    else if (s.ifFalse) s.ifFalse->visit(*this);
                    return;
                }
            }
            if (s.conditions.size() == 1 && !s.conditions[0].pattern) {
                if (auto value = model::elaboratedCondition(*s.conditions[0].expr, ec)) {
                    if (*value) s.ifTrue.visit(*this);
                    else if (s.ifFalse) s.ifFalse->visit(*this);
                    return;
                }
            }
            if (s.conditions.size() == 1 && !s.conditions[0].pattern && alwaysFalse(*s.conditions[0].expr)) {
                if (s.ifFalse) s.ifFalse->visit(*this);
                return;
            }
            size_t depth = control.size();
            for (auto& c : s.conditions) stepsIn(*c.expr);
            for (auto& c : s.conditions) pushControl(*c.expr, Branch::Taken, s.conditions.size() == 1 && !c.pattern);
            s.ifTrue.visit(*this);
            control.resize(depth);
            if (s.ifFalse) {
                for (auto& c : s.conditions) pushControl(*c.expr, Branch::NotTaken, s.conditions.size() == 1 && !c.pattern);
                s.ifFalse->visit(*this);
                control.resize(depth);
            }
        }
        void handle(const CaseStatement& s) {
            size_t depth = control.size();
            stepsIn(s.expr);
            pushControl(s.expr, Branch::None, false);
            for (auto& [sym, oi] : control.back().ops) { oi.op = Op::Compare; oi.inv = 2; }   // the selector is compared with the items
            // An arm executes only if no earlier arm matched: it depends on its own and all earlier item expressions.
            for (auto& g : s.items) {
                size_t first = control.size();
                for (auto* e : g.expressions) pushControl(*e, Branch::Taken, false);
                g.stmt->visit(*this);
                for (size_t k = first; k < control.size(); k++) control[k].br = Branch::NotTaken;   // later arms run only if this one did not match
            }
            if (s.defaultCase) s.defaultCase->visit(*this);
            control.resize(depth);
        }
        // The values the loop variables take in each iteration of a loop whose bounds and step are constant (at most
        // kMaxUnrolledIterations iterations), found by running the header in the constant evaluator. A loop is not unrolled when its body
        // can leave it early, writes its variables or calls a user subroutine: it is then walked once with the variables unknown, and the
        // dependencies it makes stay approximate.
        using LoopBinding = Builder::LoopBinding;
        static constexpr size_t kMaxUnrolledIterations = 256;
        // Over one procedure, the body walks of all its unrolled loops, nested ones counted once per enclosing iteration (an inner
        // loop of 64 under an outer one of 64 costs 4096): beyond it a loop is walked once (approximate), as one too long is.
        static constexpr size_t kUnrollWalkBudget = 2048;
        size_t unrollWalks = 0, unrollFactor = 1;
        bool unrollIterations(const ForLoopStatement& s, std::vector<LoopBinding>& iterations) {
            return b.unrollForLoop(s, ec, iterations);
        }
        void handle(const ForLoopStatement& s) {
            size_t depth = control.size();
            for (const auto* variable : s.loopVars) b.m.loopVariables.insert(variable);
            std::vector<LoopBinding> iterations;
            if (unrollIterations(s, iterations) && iterations.size() * unrollFactor + unrollWalks <= kUnrollWalkBudget) {
                // each iteration is walked with its variables bound, so a select on one is exact
                unrollWalks += iterations.size() * unrollFactor;
                const size_t savedFactor = unrollFactor;
                unrollFactor *= std::max<size_t>(1, iterations.size());
                const auto saved = b.boundLoops;
                for (const auto& binding : iterations) {
                    for (size_t k = saved.size(); k < binding.size(); ++k) ec.createLocal(binding[k].first, binding[k].second);
                    b.boundLoops = binding;
                    s.body.visit(*this);
                    for (size_t k = saved.size(); k < binding.size(); ++k) ec.deleteLocal(binding[k].first);
                }
                b.boundLoops = saved;
                unrollFactor = savedFactor;
                control.resize(depth);
                return;
            }
            for (auto* i : s.initializers) i->visit(*this);
            if (s.stopExpr) { stepsIn(*s.stopExpr); pushControl(*s.stopExpr, Branch::None, false); }
            s.body.visit(*this);
            for (auto* st : s.steps) st->visit(*this);
            control.resize(depth);
        }
        void handle(const WhileLoopStatement& s) {
            size_t depth = control.size();
            stepsIn(s.cond);
            pushControl(s.cond, Branch::None, false);
            s.body.visit(*this);
            control.resize(depth);
        }
        void handle(const DoWhileLoopStatement& s) {
            size_t depth = control.size();
            stepsIn(s.cond);
            pushControl(s.cond, Branch::None, false);
            s.body.visit(*this);
            control.resize(depth);
        }
        unsigned jumpGuardDepth = 0;
        const Statement* outerTimed = nullptr;
        bool hasBodyTiming = false;
        bool hasStmtTiming = false;
        void handle(const StatementList& list) {
            unsigned added = 0;
            for (auto* child : list.list) {
                child->visit(*this);
                JumpFinder jumps;
                child->visit(jumps);
                StuckBodyChecker unsupported;
                unsupported.outerTimed = outerTimed;
                child->visit(unsupported);
                if (jumps.found || unsupported.unsafe) {
                    ++jumpGuardDepth;
                    ++added;
                }
            }
            jumpGuardDepth -= added;
        }
        void handle(const TimedStatement& s) {
            if (&s != outerTimed) {
                hasBodyTiming = true;
                hasStmtTiming = true;
                ++jumpGuardDepth;
                visitDefault(s);
                --jumpGuardDepth;
                return;
            }
            visitDefault(s);
        }
        void handle(const WaitStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const WaitForkStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const WaitOrderStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const ConcurrentAssertionStatement& s) {
            if (s.assertionKind == AssertionKind::Expect) { hasBodyTiming = true; hasStmtTiming = true; }
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const ImmediateAssertionStatement& s) {
            s.cond.visit(*this);
            if (s.ifTrue || s.ifFalse) {
                ++jumpGuardDepth;
                if (s.ifTrue) s.ifTrue->visit(*this);
                if (s.ifFalse) s.ifFalse->visit(*this);
                --jumpGuardDepth;
            }
        }
        void handle(const PatternCaseStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const RandCaseStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const ForeverLoopStatement& s) {
            hasBodyTiming = true;
            hasStmtTiming = true;
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
        }
        void handle(const BlockStatement& s) {
            if (s.blockKind != StatementBlockKind::Sequential) {
                hasBodyTiming = true;
                hasStmtTiming = true;
                ++jumpGuardDepth;
                visitDefault(s);
                --jumpGuardDepth;
                return;
            }
            visitDefault(s);
        }
        void handle(const RepeatLoopStatement& s) {
            size_t depth = control.size();
            stepsIn(s.count);
            pushControl(s.count, Branch::None, false);
            ++jumpGuardDepth;
            s.body.visit(*this);
            --jumpGuardDepth;
            control.resize(depth);
        }
        void handle(const ForeachLoopStatement& s) {
            size_t depth = control.size();
            for (const auto& d : s.loopDims) if (d.loopVar) b.m.loopVariables.insert(d.loopVar);
            std::vector<LoopBinding> iterations;
            if (b.unrollForeachLoop(s, ec, iterations) && iterations.size() * unrollFactor + unrollWalks <= kUnrollWalkBudget) {
                unrollWalks += iterations.size() * unrollFactor;
                const size_t savedFactor = unrollFactor;
                unrollFactor *= std::max<size_t>(1, iterations.size());
                const auto saved = b.boundLoops;
                for (const auto& binding : iterations) {
                    for (size_t k = saved.size(); k < binding.size(); ++k) ec.createLocal(binding[k].first, binding[k].second);
                    b.boundLoops = binding;
                    s.body.visit(*this);
                    for (size_t k = saved.size(); k < binding.size(); ++k) ec.deleteLocal(binding[k].first);
                }
                b.boundLoops = saved;
                unrollFactor = savedFactor;
                control.resize(depth);
                return;
            }
            ++jumpGuardDepth;
            visitDefault(s);
            --jumpGuardDepth;
            control.resize(depth);
        }
        void callEffects(const CallExpression& c) {
            if (!c.isSystemCall()) {
                Assign conditions;
                b.calledConditions(c, ec, conditions);
                if (!conditions.calledConditions.empty()) out.push_back(std::move(conditions));
                const auto& summary = b.summaryFor(c);
                Assign effect;
                effect.called = true;
                effect.unattributedWrite = summary.unboundedWrites || c.thisClass();
                for (const auto& path : summary.writes)
                    if (!b.callPathResolved(path)) effect.unattributedWrite = true;
                for (const auto& path : summary.writes) if (b.callPathResolved(path)) effect.targets.push_back(path);
                const auto* sub = std::get<const SubroutineSymbol*>(c.subroutine);
                if (sub && c.hasOutputArgs()) {
                    auto args = sub->getArguments();
                    for (size_t i = 0; i < args.size() && i < c.arguments().size(); ++i) {
                        if (args[i]->direction == ArgumentDirection::In) continue;
                        const Expression* target = c.arguments()[i];
                        if (target->kind == ExpressionKind::Assignment) target = &target->as<AssignmentExpression>().left();
                        auto paths = b.rawPathsOf(*target, ec, true);
                        effect.targets.insert(effect.targets.end(), paths.begin(), paths.end());
                    }
                }
                if (!effect.targets.empty() || effect.unattributedWrite) {
                    // The write set is known, but which input reaches which write is not expanded: every state the body
                    // reads and every input or inout argument is taken to reach every written target. These edges are
                    // approximate, so a loop that needs one is Unresolved, never a proven cycle.
                    effect.approximate = true;
                    for (const auto& path : summary.reads) if (b.callPathResolved(path)) effect.dataReads.push_back(path);
                    if (sub) {
                        auto args = sub->getArguments();
                        for (size_t i = 0; i < args.size() && i < c.arguments().size(); ++i) {
                            const Expression* arg = c.arguments()[i];
                            if (args[i]->direction == ArgumentDirection::Out) {
                                if (arg->kind == ExpressionKind::Assignment) arg = &arg->as<AssignmentExpression>().left();
                                for (auto& p : b.pathsOf(*arg, ec, false))   // a selector on an output argument is read
                                    if (std::find(effect.targets.begin(), effect.targets.end(), p) == effect.targets.end()) effect.ctrlReads.push_back(p);
                            } else {
                                for (auto& p : b.pathsOf(*arg, ec, false)) effect.dataReads.push_back(p);
                            }
                        }
                    }
                    effect.uncertainCalls = b.callIssues(c, ctx);
                    effect.loc = toLoc(*b.sm, c.sourceRange.start());
                    for (const auto& ctl : control) {
                        effect.conditions.push_back({ctl.loc, ctl.paths});
                        effect.ctrlReads.insert(effect.ctrlReads.end(), ctl.paths.begin(), ctl.paths.end());
                        effect.uncertainCalls.insert(effect.uncertainCalls.end(), ctl.uncertainCalls.begin(), ctl.uncertainCalls.end());
                    }
                    out.push_back(std::move(effect));
                } else b.callIssue(c, ctx);
            }
        }
        void handle(const CallExpression& c) {
            callEffects(c);
            visitDefault(c);
        }
        // ++ and -- inside a condition or selector run unconditionally before the branch they decide: each is a read and a write
        void stepsIn(const Expression& e) {
            if (!modelSteps) return;
            struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
                std::vector<const UnaryExpression*> found;
                void handle(const UnaryExpression& u) {
                    if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                        u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) found.push_back(&u);
                    visitDefault(u);
                }
            } f;
            e.visit(f);
            for (auto* u : f.found) stepAssign(*u);
        }
        void handle(const UnaryExpression& u) {
            stepAssign(u);
            visitDefault(u);
        }
        void stepAssign(const UnaryExpression& u) {
            const bool step = u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                              u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement;
            if (step && modelSteps) {
                // x++ is x = x + 1: it reads the bits it writes
                Assign as;
                as.loc = toLoc(*b.sm, u.sourceRange.start());
                as.afterPossibleExit = jumpGuardDepth > 0;
                as.targets = b.pathsOf(u.operand(), ec, true);
                as.approximate = b.hasDynamicSelect(u.operand(), ec);
                as.booleanValue = b.unknownBoolean(u.operand(), ec, ctx);
                as.uncertainCalls = b.callIssues(u.operand(), ctx);
                for (auto& p : b.pathsOf(u.operand(), ec, false)) {
                    if (std::binary_search(as.targets.begin(), as.targets.end(), p)) as.dataReads.push_back(p);
                    else as.ctrlReads.push_back(p);
                }
                for (auto& t : as.targets) { as.dataReads.push_back(t); noteOp(as.dataOps, t.sym, Op::Arith, 2); }
                for (auto& p : as.ctrlReads) noteOp(as.ctrlOps, p.sym, Op::Select, 2);
                for (auto& c : control) {
                    as.conditions.push_back({c.loc, c.paths});
                    as.uncertainCalls.insert(as.uncertainCalls.end(), c.uncertainCalls.begin(), c.uncertainCalls.end());
                    GuardCondition fact;
                    fact.predicate = c.br == Branch::NotTaken ? b.boolFact(BoolFact::Kind::Not, {c.predicate}) : c.predicate;
                    for (const auto& path : c.paths)
                        for (auto node : b.nodesFor(ctx, path)) fact.dependencies.push_back(node);
                    as.booleanGuards.push_back(std::move(fact));
                    for (auto& p : c.paths) as.ctrlReads.push_back(p);
                    for (auto& [sym, oi] : c.ops) noteOp(as.ctrlOps, sym, oi.op, oi.inv, c.br);
                }
                for (auto* v : {&as.dataReads, &as.ctrlReads}) {
                    std::sort(v->begin(), v->end());
                    v->erase(std::unique(v->begin(), v->end()), v->end());
                }
                if (!as.targets.empty()) out.push_back(std::move(as));
            }
        }
        void recordAssign(const Expression* lhs, std::vector<Path> targets, std::vector<BitRef> lhsBits,
                          const Expression& rhs, SourceLocation loc, bool nonBlocking, bool compound,
                          bool lhsIntegral, uint64_t lhsWidth, const AssignmentExpression* asg = nullptr) {
            Assign as;
            as.asg = asg;
            as.loc = loc;
            as.nonBlocking = nonBlocking;
            const bool hasIntraTiming = asg && asg->timingControl != nullptr;
            if (hasIntraTiming) {
                hasBodyTiming = true;
                const auto tk = asg->timingControl->kind;
                const bool isPlainNbaDelay = nonBlocking && (tk == TimingControlKind::Delay || tk == TimingControlKind::Delay3);
                if (!isPlainNbaDelay) hasStmtTiming = true;
            }
            as.afterPossibleExit = jumpGuardDepth > 0 || hasIntraTiming;
            const bool lhsDynamic = lhs && b.hasDynamicSelect(*lhs, ec);
            const bool lhsNoCalls = !lhs || b.callsIn(*lhs).empty();
            as.approximate = lhsDynamic || b.hasDynamicSelect(rhs, ec) || !b.callsIn(rhs).empty();
            as.booleanValue = b.booleanOf(rhs, ec, ctx);
            as.uncertainCalls = b.callIssues(rhs, ctx);
            if (lhs) {
                auto selectorCalls = b.callIssues(*lhs, ctx);
                as.uncertainCalls.insert(as.uncertainCalls.end(), selectorCalls.begin(), selectorCalls.end());
            }
            as.targets = std::move(targets);
            ConditionalReads conditions(b, ec, as);
            rhs.visit(conditions);
            // A ternary is a procedural hold only when one complete data arm
            // is the exact target itself. Preserve its selector as a guard so
            // payload qualification can reason about the data-moving arm.
            const Expression* ternary = &rhs;
            while (ternary->kind == ExpressionKind::Conversion) {
                const auto& conversion = ternary->as<ConversionExpression>();
                if (!conversion.type || !conversion.operand().type ||
                    conversion.type->getBitstreamWidth() != conversion.operand().type->getBitstreamWidth()) break;
                ternary = &conversion.operand();
            }
            if (!compound && !lhsDynamic && lhsNoCalls && b.hasDecomposableMux(rhs, ec) &&
                !lhsBits.empty() &&
                std::all_of(lhsBits.begin(), lhsBits.end(), [](const BitRef& bit) { return bit.sym != nullptr && !bit.sym->getType().isUnpackedArray(); }) &&
                !b.collapse(*lhsBits.front().sym, widthOf(*lhsBits.front().sym))) {
                auto arms = b.decomposeExprArms(rhs, ec, ctx, lhsBits.size());
                if (arms.size() == lhsBits.size()) {
                    for (size_t i = 0; i < lhsBits.size(); ++i)
                        as.qualifierArms.emplace_back(b.nodeForBit(ctx, lhsBits[i]), std::move(arms[i]));
                }
            }
            if (as.qualifierArms.empty() && !compound && ternary->kind == ExpressionKind::ConditionalOp) {
                const auto& conditional = ternary->as<ConditionalExpression>();
                if (conditional.conditions.size() == 1 && !conditional.conditions[0].pattern && as.targets.size() == 1) {
                    auto sameTarget = [&](const Expression& arm) {
                        std::vector<BitRef> armBits;
                        return !lhsBits.empty() && b.bitList(arm, ec, armBits) &&
                               armBits.size() == lhsBits.size() &&
                               std::equal(lhsBits.begin(), lhsBits.end(), armBits.begin(),
                                          [](const BitRef& target, const BitRef& value) {
                                              return target.sym && target.sym == value.sym && target.bit == value.bit;
                                          });
                    };
                    const bool leftHolds = sameTarget(conditional.left());
                    const bool rightHolds = sameTarget(conditional.right());
                    if (leftHolds != rightHolds) {
                        GuardCondition guard;
                        guard.predicate = b.booleanOf(*conditional.conditions[0].expr, ec, ctx);
                        if (leftHolds) guard.predicate = b.boolFact(BoolFact::Kind::Not, {guard.predicate});
                        for (const auto& path : b.pathsOf(*conditional.conditions[0].expr, ec, false))
                            for (auto node : b.nodesFor(ctx, path)) guard.dependencies.push_back(node);
                        as.booleanGuards.push_back(std::move(guard));
                    }
                }
            }
            // selector reads on the LHS (e.g. mem[addr] <= d) influence the write
            // A constant selector reads nothing at run time, even if it names a signal inside $bits.
            std::vector<Path> all;
            if (lhs && (as.approximate || lhsDynamic)) all = b.pathsOf(*lhs, ec, false);
            for (auto& p : all)
                if (!std::binary_search(as.targets.begin(), as.targets.end(), p)) as.ctrlReads.push_back(p);
            for (auto& p : b.pathsOf(rhs, ec, false)) as.dataReads.push_back(p);
            // A computed index makes every bit of its vector a possible source, but a plain static read beside it is still a certain
            // one: `y = a[i] | a[0]` depends on a[0] whatever i is. Only for a one-bit result, where every such read reaches the bit.
            if (as.approximate && !compound && b.callsIn(rhs).empty() && !lhsDynamic && lhsWidth == 1)
                as.preciseReads = b.rawPathsOf(b.modeExpr(rhs, ec), ec, false, true);
            // `x = f(a[i]) ^ x`: whatever the computed index does inside one operand, a plain operand of a chain of bitwise operators of
            // the target's own width reaches the target bit for bit. Those reads are certain, so a loop they close is not approximate.
            if (as.approximate && !compound && b.callsIn(rhs).empty() && !lhsDynamic && lhsIntegral &&
                !lhsBits.empty() && std::all_of(lhsBits.begin(), lhsBits.end(), [](const BitRef& r) { return r.sym != nullptr; })) {
                std::vector<const Expression*> leaves;
                std::function<void(const Expression&)> chain = [&](const Expression& x0) {
                    const Expression& x = x0.unwrapImplicitConversions();
                    if (x.kind == ExpressionKind::BinaryOp) {
                        const auto op = x.as<BinaryExpression>().op;
                        if (op == BinaryOperator::BinaryXor || op == BinaryOperator::BinaryXnor || op == BinaryOperator::BinaryAnd || op == BinaryOperator::BinaryOr) {
                            chain(x.as<BinaryExpression>().left());
                            chain(x.as<BinaryExpression>().right());
                        }
                    }
                    else if (x.kind == ExpressionKind::UnaryOp && x.as<UnaryExpression>().op == UnaryOperator::BitwiseNot) chain(x.as<UnaryExpression>().operand());
                    else if (x.kind == ExpressionKind::NamedValue) leaves.push_back(&x);
                };
                chain(rhs);
                for (const auto* leaf : leaves) {
                    if (b.hasDynamicSelect(*leaf, ec) || !leaf->type || leaf->type->getBitstreamWidth() != lhsBits.size()) continue;
                    std::vector<BitRef> R;
                    if (b.bitList(*leaf, ec, R) && R.size() == lhsBits.size())
                        for (size_t i = 0; i < lhsBits.size(); i++)
                            if (R[i].sym) as.preciseBits.push_back({lhsBits[i], R[i]});
                }
            }
            classify(b.modeExpr(rhs, ec), Op::None, 0, as.dataOps);
            if (compound)
                for (auto& t : as.targets) { as.dataReads.push_back(t); noteOp(as.dataOps, t.sym, Op::Arith, 2); }
            for (auto& p : as.ctrlReads) noteOp(as.ctrlOps, p.sym, Op::Select, 2);   // selector reads on the left-hand side
            if (lhs && !as.ctrlReads.empty()) {
                as.conditions.push_back({toLoc(*b.sm, lhs->sourceRange.start()), as.ctrlReads});
                GuardCondition selector;
                selector.predicate = b.unknownBoolean(*lhs, ec, ctx);
                for (const auto& path : as.ctrlReads)
                    for (auto node : b.nodesFor(ctx, path)) selector.dependencies.push_back(node);
                as.booleanGuards.push_back(std::move(selector));
            }
            for (auto& c : control) {
                as.conditions.push_back({c.loc, c.paths});
                as.uncertainCalls.insert(as.uncertainCalls.end(), c.uncertainCalls.begin(), c.uncertainCalls.end());
                GuardCondition fact;
                fact.predicate = c.br == Branch::NotTaken ? b.boolFact(BoolFact::Kind::Not, {c.predicate}) : c.predicate;
                for (const auto& path : c.paths)
                    for (auto node : b.nodesFor(ctx, path)) fact.dependencies.push_back(node);
                as.booleanGuards.push_back(std::move(fact));
                for (auto& p : c.paths) as.ctrlReads.push_back(p);
                for (auto& [sym, oi] : c.ops) noteOp(as.ctrlOps, sym, oi.op, oi.inv, c.br);
            }
            if (!compound) {
                std::vector<BitRef> R;
                if (!lhsBits.empty() && b.bitList(rhs, ec, R) && lhsBits.size() == R.size()) {
                    as.pairsValid = true;
                    for (size_t i = 0; i < lhsBits.size(); i++) as.pairs.push_back({lhsBits[i], R[i]});
                } else if (std::vector<BitRef> I; !lhsBits.empty() && b.invertedBitMove(rhs, ec, I) && I.size() == lhsBits.size() &&
                           std::all_of(lhsBits.begin(), lhsBits.end(), [](const BitRef& bit) { return bit.sym != nullptr; })) {
                    as.flipPairsValid = true;
                    for (size_t i = 0; i < lhsBits.size(); i++) as.pairs.push_back({lhsBits[i], I[i]});
                } else if (lhsBits.size() > 1 && std::all_of(lhsBits.begin(), lhsBits.end(), [](const BitRef& bit) { return bit.sym != nullptr; })) {
                    std::vector<std::vector<Path>> deps;
                    uint64_t budget = kMaxEdgesPerAssign;
                    if (Builder::CallBits bits; b.expandWholeCall(rhs, ec, bits) && bits.size() == lhsBits.size()) {
                        // A straight-line function: each target bit depends on the argument bits it was built from.
                        as.bitDepsValid = true;
                        for (size_t i = 0; i < lhsBits.size(); i++) {
                            as.bitMoves.push_back({bits[i].move, bits[i].kind});
                            as.bitDeps.push_back({lhsBits[i], std::move(bits[i].reads)});
                        }
                        if (b.callsIn(rhs).size() == 1 && !b.hasDynamicSelect(rhs, ec)) as.approximate = lhsDynamic;
                    } else if (b.bitwiseDeps(rhs, ec, deps, budget) && deps.size() == lhsBits.size()) {
                        as.bitDepsValid = true;
                        as.bitDepExact = b.exactBitsOf(rhs, ec, lhsBits.size());
                        as.bitMoves = b.bitMoves(rhs, ec, lhsBits.size());
                        for (size_t i = 0; i < lhsBits.size(); i++) as.bitDeps.push_back({lhsBits[i], std::move(deps[i])});
                    }
                }
            }
            for (auto* v : {&as.dataReads, &as.ctrlReads}) {
                std::sort(v->begin(), v->end());
                v->erase(std::unique(v->begin(), v->end()), v->end());
            }
            if (!as.targets.empty()) out.push_back(std::move(as));
            rhs.visit(*this);   // nested assignments / calls on the RHS
        }
        void handle(const VariableDeclStatement& vd) {
            const auto& sym = vd.symbol;
            if (!isGraphSymbol(sym)) return;
            const Expression* init = sym.getInitializer();
            if (!init) return;
            const uint64_t w = widthOf(sym);
            std::vector<BitRef> targetBits;
            if (w && !sym.getType().isUnpackedArray() && !b.collapse(sym, w)) {
                targetBits.reserve(w);
                for (uint64_t bit = w; bit-- > 0;)
                    targetBits.push_back({&sym, static_cast<uint32_t>(bit)});
            }
            const Path wholePath{&sym, 0, w ? w - 1 : 0};
            recordAssign(nullptr, {wholePath}, std::move(targetBits), *init,
                         toLoc(*b.sm, vd.sourceRange.start()), false, false,
                         sym.getType().isIntegral(), w);
        }
        void handle(const AssignmentExpression& a) {
            for (const auto* call : b.callsIn(a.left())) callEffects(*call);
            std::vector<BitRef> lhsBits;
            b.bitList(a.left(), ec, lhsBits);
            const bool lhsIntegral = a.left().type && a.left().type->isIntegral();
            const uint64_t lhsWidth = a.left().type ? a.left().type->getBitstreamWidth() : 0;
            recordAssign(&a.left(), b.pathsOf(a.left(), ec, true), std::move(lhsBits), a.right(),
                         toLoc(*b.sm, a.sourceRange.start()), a.isNonBlocking(), a.isCompound(),
                         lhsIntegral, lhsWidth, &a);
        }
    };

    // ---------------------------------------------------------------- procedures
    static const Statement* firstMeaningful(const Statement* s) {
        while (s) {
            if (s->kind == StatementKind::Block) { s = &s->as<BlockStatement>().body; continue; }
            if (s->kind == StatementKind::List) {
                const auto& l = s->as<StatementList>().list;
                const Statement* next = nullptr;
                for (auto* c : l) { if (c->kind != StatementKind::VariableDeclaration) { next = c; break; } }
                if (!next) return nullptr;
                s = next; continue;
            }
            return s;
        }
        return nullptr;
    }

    struct Event { int edge; Path path; bool ok; slang::SourceLocation loc; std::vector<Path> iff; bool hasIff; std::vector<uint32_t> uncertainCalls; const Expression* expression; };

    void collectEvents(const TimingControl& t, EvalContext& ec, std::vector<Event>& out, const InstanceSymbol* ctx) {
        if (t.kind == TimingControlKind::SignalEvent) {
            auto& se = t.as<SignalEventControl>();
            Event e{0, {}, false, t.sourceRange.start(), {}, se.iffCondition != nullptr, {}, &se.expr};
            if (se.iffCondition) e.uncertainCalls = callIssues(*se.iffCondition, ctx);
            if (se.iffCondition) e.iff = pathsOf(*se.iffCondition, ec, false);
            e.edge = se.edge == EdgeKind::PosEdge ? 1 : se.edge == EdgeKind::NegEdge ? 2 : se.edge == EdgeKind::BothEdges ? 3 : 0;
            // Read sets do not prove a call's clock value or edge polarity.
            // Keep the existing clock-root extraction separate from body reads.
            auto ps = rawPathsOf(se.expr, ec, false);
            if (ps.size() == 1) { e.path = ps[0]; e.ok = true; }
            out.push_back(e);
        } else if (t.kind == TimingControlKind::EventList) {
            for (auto* c : t.as<EventListControl>().events) collectEvents(*c, ec, out, ctx);
        }
    }

    // Scheduling facts are expanded separately from the conservative connectivity summaries.
    // The latter intentionally keep cross-product read/write dependencies approximate.
    struct WriteInventory : ASTVisitor<WriteInventory, VisitFlags::AllGood> {
        Builder& b;
        const ProceduralBlockSymbol& process;
        const InstanceSymbol* instance;
        EvalContext ec;
        struct TargetPath { Path path; bool throughCopy = false; };
        std::map<const ValueSymbol*, std::vector<TargetPath>> bindings;
        std::set<const ValueSymbol*> dynamicBindings;
        std::set<const ValueSymbol*> copyFormals;
        std::vector<const Statement*> scopes;
        std::vector<const SubroutineSymbol*> calls;
        unsigned conditionalDepth = 0;
        unsigned forkDepth = 0;
        SourceLocation callLoc, outerLoc;
        WriteInventory(Builder& builder, const ProceduralBlockSymbol& p, const InstanceSymbol* i)
            : b(builder), process(p), instance(i), ec(p) {}
        std::vector<TargetPath> targets(const Expression& e) {
            std::vector<TargetPath> result;
            std::vector<Path> paths;
            ValuePath::visitPaths(e, ec, [&](const ValuePath& vp) {
                const auto* sym = vp.rootSymbol();
                if (!sym) return;
                auto width = widthOf(*sym);
                Path path{sym, 0, width ? width - 1 : 0};
                if (vp.lsp && width) { path.lo = vp.lspBounds.first; path.hi = vp.lspBounds.second; }
                if (path.lo > path.hi) std::swap(path.lo, path.hi);
                paths.push_back(path);
            }, true);
            for (const auto& path : paths) {
                if (auto it = bindings.find(path.sym); it != bindings.end()) {
                    if (it->second.size() > 1 && (path.lo != 0 || path.hi + 1 != widthOf(*path.sym))) {
                        unknown("formal slice of concatenated actual target cannot be attributed");
                        continue;
                    }
                    for (auto actual : it->second) {
                        // Map a formal slice into the selected actual, retaining the containing object.
                        if (!dynamicBindings.count(path.sym) && staticTarget(e) && path.hi < actual.path.hi - actual.path.lo + 1) {
                            actual.path.hi = actual.path.lo + path.hi;
                            actual.path.lo += path.lo;
                        }
                        actual.throughCopy = actual.throughCopy || copyFormals.count(path.sym);
                        result.push_back(actual);
                    }
                } else if (isModuleLevel(*path.sym) ||
                           (path.sym->getParentScope() &&
                            (path.sym->getParentScope()->asSymbol().kind == SymbolKind::Package ||
                             path.sym->getParentScope()->asSymbol().kind == SymbolKind::CompilationUnit))) result.push_back({path, false});
                else if (path.sym->kind == SymbolKind::FormalArgument) unknown("write target formal cannot be bound to its actual");
            }
            return result;
        }
        bool staticTarget(const Expression& e) {
            bool exact = true;
            ValuePath::visitPaths(e, ec, [&](const ValuePath& vp) {
                exact = exact && vp.isFullyStatic() && !dynamicBindings.count(vp.rootSymbol());
            }, true);
            return exact;
        }
        // The non-local variables a subroutine and everything it calls may write. `unattributed` is set when a write goes
        // through a formal or a call whose body cannot be examined, so the targets are not all known.
        struct MayWrite { std::set<const ValueSymbol*> targets; bool unattributed = false; };
        MayWrite mayWrite(const SubroutineSymbol& root) {
            struct Scan : ASTVisitor<Scan, VisitFlags::AllGood> {
                WriteInventory& owner;
                MayWrite& out;
                std::set<const SubroutineSymbol*> seen;
                Scan(WriteInventory& o, MayWrite& m) : owner(o), out(m) {}
                void note(const Expression& target) {
                    ValuePath::visitPaths(target, owner.ec, [&](const ValuePath& vp) {
                        const auto* sym = vp.rootSymbol();
                        if (!sym) return;
                        if (sym->kind == SymbolKind::FormalArgument) { out.unattributed = true; return; }
                        const auto* scope = sym->getParentScope();
                        if (isModuleLevel(*sym) || (scope && (scope->asSymbol().kind == SymbolKind::Package ||
                                                              scope->asSymbol().kind == SymbolKind::CompilationUnit)))
                            out.targets.insert(sym);
                    }, true);
                }
                void handle(const AssignmentExpression& a) { note(a.left()); visitDefault(a); }
                void handle(const UnaryExpression& u) {
                    if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                        u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) note(u.operand());
                    visitDefault(u);
                }
                void handle(const CallExpression& call) {
                    if (!call.isSystemCall()) {
                        const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
                        if (!sub || call.thisClass() || sub->isVirtual() ||
                            sub->flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) || sub->getBody().bad())
                            out.unattributed = true;
                        else if (seen.insert(sub).second) sub->getBody().visit(*this);
                    }
                    visitDefault(call);
                }
            };
            MayWrite result;
            Scan scan(*this, result);
            scan.seen.insert(&root);
            root.getBody().visit(scan);
            return result;
        }
        void unknown(std::string reason, SourceLocation at, std::vector<const ValueSymbol*> targets = {}, bool known = false) {
            ProceduralWrite w;
            w.process = &process; w.instance = instance; w.forkOwnership = forkDepth > 0; w.loc = at;
            w.called = true; w.fromCall = true; w.reason = std::move(reason);
            w.mayWrite = std::move(targets); w.mayWriteKnown = known;
            w.scopes = scopes; w.conditionalDepth = conditionalDepth; w.afterPossibleExit = exitGuard > 0;
            b.m.proceduralWrites.push_back(std::move(w));
        }
        void unknown(std::string reason) {
            ProceduralWrite w;
            w.process = &process; w.instance = instance; w.forkOwnership = forkDepth > 0; w.loc = callLoc;
            w.called = true; w.fromCall = true; w.reason = std::move(reason);
            w.scopes = scopes; w.conditionalDepth = conditionalDepth;
            b.m.proceduralWrites.push_back(std::move(w));
        }
        void handle(const AssignmentExpression& a) {
            // Output wrappers in streaming/pattern lvalues are bindings, not '=' writes.
            if (a.isLValueArg()) { visitDefault(a); return; }
            const auto value = a.right().eval(ec);
            for (const auto& target : targets(a.left())) {
                const auto& path = target.path;
                ProceduralWrite w;
                w.process = &process; w.instance = instance; w.forkOwnership = forkDepth > 0; w.target = path.sym;
                w.loc = toLoc(*b.sm, a.sourceRange.start());
                w.nonBlocking = a.isNonBlocking(); w.fromCall = !calls.empty();
                w.throughCopyFormal = target.throughCopy;
                w.lo = path.lo; w.hi = path.hi;
                w.targetExpr = &a.left(); w.exactTarget = staticTarget(a.left());
                w.value = a.isCompound() ? nullptr : &a.right();
                w.constantValue = value.isInteger() && !value.hasUnknown();
                w.partialTarget = !staticTarget(a.left()) || path.lo != 0 || path.hi + 1 != widthOf(*path.sym) ||
                                  (a.left().kind != ExpressionKind::NamedValue && a.left().kind != ExpressionKind::HierarchicalValue);
                w.wholeConstant = !w.partialTarget && value.isInteger() && !value.hasUnknown();
                w.conditionalDepth = conditionalDepth; w.scopes = scopes; w.afterPossibleExit = exitGuard > 0;
                b.m.proceduralWrites.push_back(std::move(w));
            }
            visitDefault(a);
        }
        void handle(const UnaryExpression& u) {
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) {
                for (const auto& target : targets(u.operand())) {
                    const auto& path = target.path;
                    ProceduralWrite w;
                    w.process = &process; w.instance = instance; w.forkOwnership = forkDepth > 0; w.target = path.sym;
                    w.loc = toLoc(*b.sm, u.sourceRange.start()); w.fromCall = !calls.empty();
                    w.throughCopyFormal = target.throughCopy;
                    w.lo = path.lo; w.hi = path.hi; w.scopes = scopes;
                    w.targetExpr = &u.operand(); w.exactTarget = staticTarget(u.operand());
                    w.partialTarget = !w.exactTarget || path.lo != 0 || path.hi + 1 != widthOf(*path.sym) ||
                                      (u.operand().kind != ExpressionKind::NamedValue && u.operand().kind != ExpressionKind::HierarchicalValue);
                    w.conditionalDepth = conditionalDepth; w.afterPossibleExit = exitGuard > 0;
                    b.m.proceduralWrites.push_back(std::move(w));
                }
            }
            visitDefault(u);
        }
        static bool returns(const Statement& s) {
            if (s.kind == StatementKind::Return) return true;
            if (s.kind == StatementKind::Block) return returns(s.as<BlockStatement>().body);
            if (s.kind == StatementKind::List) {
                for (auto* child : s.as<StatementList>().list) if (returns(*child)) return true;
            }
            if (s.kind == StatementKind::Conditional) {
                const auto& c = s.as<ConditionalStatement>();
                return c.ifFalse && returns(c.ifTrue) && returns(*c.ifFalse);
            }
            return false;
        }
        static bool mayJump(const Statement& s) {
            JumpFinder jumps;
            s.visit(jumps);
            return jumps.found;
        }
        unsigned uncertainReturns = 0;
        unsigned exitGuard = 0;   // statements of the enclosing lists that may have ended the list before the one visited now
        void handle(const BlockStatement& block) {
            bool parallel = block.blockKind != StatementBlockKind::Sequential;
            if (parallel) ++forkDepth;
            visitDefault(block);
            if (parallel) --forkDepth;
        }
        void handle(const StatementList& list) {
            unsigned added = 0;
            for (auto* child : list.list) {
                child->visit(*this);
                if (returns(*child)) break;
                if (mayJump(*child)) { ++exitGuard; ++added; }
            }
            exitGuard -= added;
        }
        void branch(const Statement& s) {
            scopes.push_back(&s); s.visit(*this); scopes.pop_back();
        }
        void handle(const ConditionalStatement& c) {
            for (const auto& condition : c.conditions) condition.expr->visit(*this);
            ++conditionalDepth;
            scopes.push_back(&c);   // the `if` itself, then the branch it selects
            branch(c.ifTrue);
            if (c.ifFalse) branch(*c.ifFalse);
            scopes.pop_back();
            --conditionalDepth;
        }
        template<class T> void handle(const T& node) {
            constexpr bool guarded = std::is_same_v<T, ForLoopStatement> || std::is_same_v<T, WhileLoopStatement> ||
                                     std::is_same_v<T, DoWhileLoopStatement> || std::is_same_v<T, CaseStatement> ||
                                     std::is_same_v<T, RepeatLoopStatement> || std::is_same_v<T, ForeachLoopStatement>;
            if constexpr (guarded) { ++conditionalDepth; scopes.push_back(&node); }
            visitDefault(node);
            if constexpr (guarded) { --conditionalDepth; scopes.pop_back(); }
        }
        // A system subroutine with output arguments, or a built-in method that changes its object, writes storage the
        // assignments do not show. The written extent is not known, so every storage argument is a target.
        void systemWrites(const CallExpression& call) {
            const auto& sub = *std::get<CallExpression::SystemCallInfo>(call.subroutine).subroutine;
            static const std::set<std::string> mutating = {"delete", "push_back", "push_front", "pop_back", "pop_front",
                                                           "insert", "shuffle", "reverse", "sort", "rsort", "putc", "randomize"};
            // A built-in method receives its object as the first argument; only a mutating one writes it.
            const bool method = !sub.name.empty() && sub.name[0] != '$';
            const auto args = call.arguments();
            std::vector<const Expression*> exprs;
            auto push = [&](const Expression* arg) {
                exprs.push_back(arg->kind == ExpressionKind::Assignment ? &arg->as<AssignmentExpression>().left() : arg);
            };
            if (method && !args.empty() && mutating.count(sub.name)) push(args[0]);
            if (sub.hasOutputArgs)
                for (size_t i = method ? 1 : 0; i < args.size(); ++i) push(args[i]);
            if (exprs.empty()) return;
            for (const auto* e : exprs)
                for (const auto& target : targets(*e)) {
                    SystemWrite w;
                    w.process = &process; w.instance = instance; w.target = target.path.sym;
                    w.loc = toLoc(*b.sm, call.sourceRange.start()); w.forkOwnership = forkDepth > 0;
                    w.lo = target.path.lo; w.hi = target.path.hi; w.name = sub.name;
                    b.m.systemWrites.push_back(std::move(w));
                }
        }
        void handle(const CallExpression& call) {
            if (call.isSystemCall()) { systemWrites(call); visitDefault(call); return; }
            const auto savedLoc = callLoc;
            callLoc = toLoc(*b.sm, call.sourceRange.start());
            if (calls.empty()) outerLoc = callLoc;
            const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
            if (!sub) unknown("subroutine has no resolved body");
            else if (call.thisClass() || sub->isVirtual()) unknown("class or virtual method dispatch cannot be resolved");
            else if (sub->flags.has(MethodFlags::DPIImport)) unknown("DPI import has no examinable body");
            else if (sub->flags.has(MethodFlags::InterfaceExtern)) unknown("extern subroutine has no examinable body");
            else if (sub->getBody().bad()) unknown("subroutine has no examinable body");
            else if (calls.size() >= 32 || std::find(calls.begin(), calls.end(), sub) != calls.end()) {
                // The expansion stops here. What the cut-off part may still write is what the callee's whole body can reach.
                auto reach = mayWrite(*sub);
                if (reach.unattributed)
                    unknown("recursive subroutine expansion reached its bound", calls.empty() ? callLoc : outerLoc);
                else if (!reach.targets.empty()) {
                    std::string names;
                    for (const auto* t : reach.targets) names += (names.empty() ? "" : ", ") + std::string(t->name);
                    unknown("recursive subroutine expansion reached its bound; it may write " + names, calls.empty() ? callLoc : outerLoc,
                            {reach.targets.begin(), reach.targets.end()}, true);
                }
            }
            else {
                auto savedBindings = bindings;
                auto savedDynamic = dynamicBindings;
                auto savedCopy = copyFormals;
                const auto formals = sub->getArguments();
                std::map<const ValueSymbol*, std::vector<TargetPath>> actuals;
                struct CopyOut { const Expression* arg; std::vector<TargetPath> paths; bool exact; };
                std::vector<CopyOut> copyOuts;
                for (size_t i = 0; i < formals.size() && i < call.arguments().size(); ++i) {
                    if (formals[i]->direction == ArgumentDirection::In) { actuals[formals[i]] = {}; continue; }
                    const Expression* arg = call.arguments()[i];
                    if (arg->kind == ExpressionKind::Assignment) arg = &arg->as<AssignmentExpression>().left();
                    actuals[formals[i]] = targets(*arg);
                    if (formals[i]->direction == ArgumentDirection::Out || formals[i]->direction == ArgumentDirection::InOut) {
                        copyOuts.push_back({arg, actuals[formals[i]], staticTarget(*arg)});
                        copyFormals.insert(formals[i]);
                    }
                    if (!staticTarget(*arg)) dynamicBindings.insert(formals[i]);
                    if (actuals[formals[i]].empty() && !arg->getSymbolReference())
                        unknown("output or ref actual target cannot be attributed");
                }
                bindings.insert(actuals.begin(), actuals.end());
                const size_t beforeBody = b.m.proceduralWrites.size();
                const unsigned beforeReturns = uncertainReturns;
                calls.push_back(sub); sub->getBody().visit(*this); calls.pop_back();
                bool uncertainReturn = uncertainReturns != beforeReturns;
                for (size_t i = beforeBody; i < b.m.proceduralWrites.size(); ++i)
                    uncertainReturn = uncertainReturn || (b.m.proceduralWrites[i].called &&
                        !b.m.proceduralWrites[i].copyBackValueUnknown);
                struct ReturnScan : ASTVisitor<ReturnScan, VisitFlags::AllGood> {
                    bool uncertain = false;
                    void handle(const TimedStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const ForeverLoopStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const WaitStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const WaitForkStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const WaitOrderStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const BlockStatement& s) {
                        // Detached children do not delay this call's copy-out.
                        if (s.blockKind != StatementBlockKind::JoinNone) visitDefault(s);
                    }
                    void handle(const WhileLoopStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const DoWhileLoopStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const ForLoopStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const RepeatLoopStatement& s) { uncertain = true; visitDefault(s); }
                    void handle(const ForeachLoopStatement& s) { uncertain = true; visitDefault(s); }
                } scan;
                sub->getBody().visit(scan);
                uncertainReturn = uncertainReturn || scan.uncertain;
                if (scan.uncertain) ++uncertainReturns;
                for (const auto& out : copyOuts) for (const auto& target : out.paths) {
                    // A nested copy-out into an enclosing output formal updates that formal;
                    // its actual is written only when the enclosing call returns.
                    if (target.throughCopy) continue;
                    const auto& path = target.path;
                    ProceduralWrite w;
                    w.process = &process; w.instance = instance; w.forkOwnership = forkDepth > 0; w.target = path.sym;
                    w.loc = callLoc; w.fromCall = true; w.lo = path.lo; w.hi = path.hi;
                    w.targetExpr = out.arg; w.exactTarget = out.exact;
                    w.partialTarget = !out.exact || path.lo != 0 || path.hi + 1 != widthOf(*path.sym);
                    w.called = uncertainReturn;
                    w.reason = "output copy-back value or return is not established";
                    w.scopes = scopes; w.conditionalDepth = conditionalDepth; w.afterPossibleExit = exitGuard > 0;
                    b.m.copyBackWrites.push_back(w);
                    unknown("output copy-back value is not established", callLoc, {path.sym}, true);
                    b.m.proceduralWrites.back().copyBackValueUnknown = true;
                }
                bindings = std::move(savedBindings);
                dynamicBindings = std::move(savedDynamic);
                copyFormals = std::move(savedCopy);
            }
            callLoc = savedLoc;
            visitDefault(call);
        }
    };
    // Read effects are separate from connectivity: they preserve the calling process and
    // do not treat by-value formals as aliases of the actual storage.
    struct ReadInventory : ASTVisitor<ReadInventory, VisitFlags::AllGood> {
        Builder& b;
        const Symbol& process;
        const InstanceSymbol* instance;
        EvalContext ec;
        struct ReadPath { const ValueSymbol* sym; uint64_t lo, hi; bool exact; bool bounded; };
        std::map<const ValueSymbol*, std::vector<ReadPath>> refs;
        std::vector<const SubroutineSymbol*> calls;
        unsigned expansions = 0;
        unsigned forkDepth = 0;
        unsigned detachedDepth = 0;
        SourceLocation callLoc;
        ReadInventory(Builder& builder, const Symbol& p, const InstanceSymbol* inst)
            : b(builder), process(p), instance(inst), ec(p) {}
        void unknown(std::string reason) {
            b.m.processReads.push_back({&process, instance, nullptr, callLoc, std::move(reason)});
        }
        std::vector<ReadPath> paths(const Expression& e) {
            std::vector<ReadPath> result;
            ValuePath::visitPaths(e, ec, [&](const ValuePath& vp) {
                const auto* sym = vp.rootSymbol();
                if (!sym) return;
                const auto width = widthOf(*sym);
                uint64_t lo = 0, hi = width ? width - 1 : 0;
                if (vp.lsp && width) { lo = vp.lspBounds.first; hi = vp.lspBounds.second; }
                if (lo > hi) std::swap(lo, hi);
                const bool exact = width && vp.lsp && vp.isFullyStatic();
                if (auto it = refs.find(sym); it != refs.end()) {
                    for (auto actual : it->second) {
                        if (exact && actual.exact && hi <= actual.hi - actual.lo) {
                            actual.hi = actual.lo + hi;
                            actual.lo += lo;
                        } else actual.exact = false;
                        result.push_back(actual);
                    }
                } else if (isModuleLevel(*sym) || (sym->getParentScope() &&
                         (sym->getParentScope()->asSymbol().kind == SymbolKind::Package ||
                          sym->getParentScope()->asSymbol().kind == SymbolKind::CompilationUnit)))
                    result.push_back({sym, lo, hi, exact, width != 0});
            }, true);
            return result;
        }
        void record(const Expression& e) {
            for (const auto& path : paths(e))
                b.m.processReads.push_back({&process, instance, path.sym, toLoc(*b.sm, e.sourceRange.start()), {}, forkDepth > 0,
                                            path.lo, path.hi, path.exact, path.bounded});
        }
        void handle(const NamedValueExpression& e) { record(e); }
        void handle(const HierarchicalValueExpression& e) { record(e); }
        template<typename T> void selected(const T& e) {
            // Visit the selected path once; targetReads walks its selector expressions separately.
            if (paths(e).empty()) visitDefault(e);
            else { record(e); targetReads(e); }
        }
        void handle(const ElementSelectExpression& e) { selected(e); }
        void handle(const RangeSelectExpression& e) { selected(e); }
        void handle(const MemberAccessExpression& e) { selected(e); }
        void targetReads(const Expression& e) {
            switch (e.kind) {
                case ExpressionKind::NamedValue: case ExpressionKind::HierarchicalValue: return;
                case ExpressionKind::ElementSelect: {
                    const auto& x = e.as<ElementSelectExpression>(); targetReads(x.value()); x.selector().visit(*this); return;
                }
                case ExpressionKind::RangeSelect: {
                    const auto& x = e.as<RangeSelectExpression>(); targetReads(x.value()); x.left().visit(*this); x.right().visit(*this); return;
                }
                case ExpressionKind::MemberAccess: targetReads(e.as<MemberAccessExpression>().value()); return;
                case ExpressionKind::Concatenation:
                    for (auto* x : e.as<ConcatenationExpression>().operands()) targetReads(*x);
                    return;
                case ExpressionKind::Conversion: targetReads(e.as<ConversionExpression>().operand()); return;
                default: unknown("assignment target read effects cannot be attributed"); return;
            }
        }
        void handle(const AssignmentExpression& a) {
            if (a.isCompound()) a.left().visit(*this); else targetReads(a.left());
            a.right().visit(*this);
        }
        void handle(const StatementList& list) {
            for (auto* child : list.list) {
                child->visit(*this);
                if (WriteInventory::returns(*child)) break;
            }
        }
        void handle(const TimedStatement& t) {
            if (!calls.empty() && !detachedDepth) unknown("timing inside a called subroutine limits read effects");
            visitDefault(t);
        }
        void handle(const WaitStatement& s) {
            if (!calls.empty() && !detachedDepth) unknown("waiting inside a called subroutine limits read effects");
            visitDefault(s);
        }
        void handle(const WaitForkStatement& s) {
            if (!calls.empty() && !detachedDepth) unknown("waiting inside a called subroutine limits read effects");
            visitDefault(s);
        }
        void handle(const WaitOrderStatement& s) {
            if (!calls.empty() && !detachedDepth) unknown("waiting inside a called subroutine limits read effects");
            visitDefault(s);
        }
        void handle(const BlockStatement& block) {
            const bool parallel = block.blockKind != StatementBlockKind::Sequential;
            const bool detached = block.blockKind == StatementBlockKind::JoinNone;
            if (parallel) ++forkDepth;
            if (detached) ++detachedDepth;
            visitDefault(block);
            if (detached) --detachedDepth;
            if (parallel) --forkDepth;
        }
        void handle(const CallExpression& call) {
            if (call.isSystemCall()) {
                auto saved = callLoc;
                callLoc = toLoc(*b.sm, call.sourceRange.start());
                const auto& name = std::get<CallExpression::SystemCallInfo>(call.subroutine).subroutine->name;
                static const std::set<std::string> pure = {"$clog2", "$signed", "$unsigned", "$countones", "$onehot", "$onehot0", "$isunknown"};
                static const std::set<std::string> typeQueries = {"$bits", "$typename", "$size", "$high", "$low", "$left", "$right", "$increment", "$dimensions"};
                if (pure.count(name)) visitDefault(call);
                else if (typeQueries.count(name) && !call.arguments().empty() &&
                         (name == "$typename" || call.arguments()[0]->type->isFixedSize())) {
                    // Slang binds the first argument as unevaluated for fixed-size type queries.
                    for (size_t i = 1; i < call.arguments().size(); ++i) call.arguments()[i]->visit(*this);
                } else unknown("system subroutine argument read effects cannot be attributed");
                callLoc = saved;
                return;
            }
            auto savedLoc = callLoc;
            callLoc = toLoc(*b.sm, call.sourceRange.start());
            const auto* sub = std::get<const SubroutineSymbol*>(call.subroutine);
            if (!sub || call.thisClass() || sub->isVirtual() ||
                sub->flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) || sub->getBody().bad()) {
                unknown("called subroutine read effects cannot be examined");
                if (sub) {
                    const auto formals = sub->getArguments();
                    for (size_t i = 0; i < call.arguments().size(); ++i) {
                        const Expression* arg = call.arguments()[i];
                        if (arg->kind == ExpressionKind::Assignment && arg->as<AssignmentExpression>().isLValueArg())
                            arg = &arg->as<AssignmentExpression>().left();
                        if (i >= formals.size()) continue;
                        auto dir = formals[i]->direction;
                        if (dir == ArgumentDirection::In || dir == ArgumentDirection::InOut) arg->visit(*this);
                        else targetReads(*arg);
                    }
                }
                callLoc = savedLoc; return;
            }
            auto savedRefs = refs;
            auto calleeRefs = refs;
            const auto formals = sub->getArguments();
            for (size_t i = 0; i < call.arguments().size(); ++i) {
                const Expression* arg = call.arguments()[i];
                if (arg->kind == ExpressionKind::Assignment && arg->as<AssignmentExpression>().isLValueArg()) arg = &arg->as<AssignmentExpression>().left();
                if (i >= formals.size()) { unknown("call argument direction is unavailable"); continue; }
                auto dir = formals[i]->direction;
                if (dir == ArgumentDirection::In || dir == ArgumentDirection::InOut) arg->visit(*this);
                else targetReads(*arg);
                if (dir == ArgumentDirection::Ref) calleeRefs[formals[i]] = paths(*arg);
                else calleeRefs[formals[i]] = {}; // input, output and inout are local copies
            }
            refs = std::move(calleeRefs);
            if (calls.size() >= 32 || expansions >= 256 || std::find(calls.begin(), calls.end(), sub) != calls.end())
                unknown("recursive subroutine read expansion reached its bound");
            else { ++expansions; calls.push_back(sub); sub->getBody().visit(*this); calls.pop_back(); }
            refs = std::move(savedRefs);
            callLoc = savedLoc;
        }
    };
    void inventoryReads(const Symbol& process, const InstanceSymbol* ctx, const Statement& body) {
        ReadInventory visitor(*this, process, ctx); body.visit(visitor);
    }
    void inventoryReads(const Symbol& process, const InstanceSymbol* ctx, const Expression& expression) {
        ReadInventory visitor(*this, process, ctx); expression.visit(visitor);
    }
    void inventoryWrites(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx, const std::vector<Assign>&) {
        WriteInventory visitor(*this, pb, ctx);
        pb.getBody().visit(visitor);
    }

    void handleProcedure(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx) {
        ProceduralBlockKind pk = pb.procedureKind;
        if (pk == ProceduralBlockKind::Initial || pk == ProceduralBlockKind::Final) {
            EvalContext ec(pb);
            std::vector<Assign> assigns;
            StmtVisitor visitor(*this, ec, assigns, ctx);
            pb.getBody().visit(visitor);
            inventoryWrites(pb, ctx, assigns);
            inventoryReads(pb, ctx, pb.getBody());
            return;
        }
        m.numProcedures++;
        EvalContext ec(pb);
        const Statement& body = pb.getBody();
        const Statement* first = firstMeaningful(&body);
        std::vector<Event> events;
        bool sequential = false;
        if (first && first->kind == StatementKind::Timed) {
            collectEvents(first->as<TimedStatement>().timing, ec, events, ctx);
            for (auto& e : events) if (e.edge) sequential = true;
        }
        if (pk == ProceduralBlockKind::AlwaysFF) sequential = true;

        std::vector<Assign> assigns;
        noteLoopCounters(body);
        curDominated.clear();
        if (pb.procedureKind != ProceduralBlockKind::AlwaysLatch) {
            const auto reads = analysis::readsOf(body);
            // what a called subroutine reads before it writes is a read of the caller's value too
            std::set<const ValueSymbol*> viaCalls;
            std::set<const SubroutineSymbol*> seen;
            struct Callees : ASTVisitor<Callees, VisitFlags::AllGood> {
                std::vector<const SubroutineSymbol*> found;
                void handle(const CallExpression& c) {
                    if (!c.isSystemCall())
                        if (const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine); sub && *sub) found.push_back(*sub);
                    visitDefault(c);
                }
            } callees;
            body.visit(callees);
            while (!callees.found.empty()) {
                const auto* sub = callees.found.back();
                callees.found.pop_back();
                if (!seen.insert(sub).second) continue;
                const auto inner = analysis::readsOf(sub->getBody());
                viaCalls.insert(inner.exposed.begin(), inner.exposed.end());
                sub->getBody().visit(callees);
            }
            for (auto* sym : reads.written) if (reads.dominated(sym) && !viaCalls.count(sym)) curDominated.insert(sym);
        }
        StmtVisitor v(*this, ec, assigns, ctx);
        v.modelSteps = !sequential;
        const bool outerEventSensitivity = first && first->kind == StatementKind::Timed &&
            (sequential ||
             first->as<TimedStatement>().timing.kind == TimingControlKind::ImplicitEvent ||
             first->as<TimedStatement>().timing.kind == TimingControlKind::SignalEvent ||
             first->as<TimedStatement>().timing.kind == TimingControlKind::EventList);
        v.outerTimed = outerEventSensitivity ? first : nullptr;
        body.visit(v);
        inventoryWrites(pb, ctx, assigns);
        inventoryReads(pb, ctx, pb.getBody());

        // A latch keeps its pre-activation value by design, so its order proves nothing.
        std::optional<EntryReads> entryReads;
        if (pk != ProceduralBlockKind::AlwaysLatch) entryReads = entryReadsOf(pb, ec, assigns);
        // the procedure's own facts must not outlive it: a later continuous assignment reading the same variable is not its own write
        struct ResetEntry { Builder& b; ~ResetEntry() { b.curEntryReads = nullptr; b.curProvenSelf = nullptr; b.curDominated.clear(); } } resetEntry{*this};
        curEntryReads = entryReads ? &*entryReads : nullptr;
        curProvenSelf = entryReads && !sequential && flow::styleOf(pb) == flow::Style::Comb ? &provenSelf : nullptr;

        ProcKind kind = sequential ? ProcKind::Sequential
                      : pk == ProceduralBlockKind::AlwaysLatch ? ProcKind::Latch : ProcKind::Comb;
        uint32_t proc = addProc(kind, pb.location, ctx);
        m.procs[proc].block = &pb;
        m.procs[proc].inst = ctx;
        if (!sequential) {
            std::unique_ptr<flow::Flow> procFlow;
            bool exactProcFlow = false;
            if (pk != ProceduralBlockKind::AlwaysLatch) {
                std::unordered_set<const ValueSymbol*> writtenOnce;
                bool hasMultiWrite = false;
                for (const auto& a : assigns) {
                    if (!a.asg) continue;
                    for (const auto& t : a.targets) {
                        if (!writtenOnce.insert(t.sym).second) { hasMultiWrite = true; break; }
                    }
                    if (hasMultiWrite) break;
                }
                if (hasMultiWrite) {
                    procFlow = std::make_unique<flow::Flow>(pb);
                    exactProcFlow = !procFlow->bad && !procFlow->approximated && !procFlow->opaque &&
                                    !procFlow->timing && !procFlow->walkedOnce;
                }
            }
            if (!exactProcFlow) {
                for (auto& a : assigns) emitAssign(ctx, a, a.targets, EdgeType::Comb, proc, pb.location);
                if (kind == ProcKind::Comb) analyseCombProcedure(pb, ctx, ec, proc);
                return;
            }
            auto isDeadWrite = [&](const Assign& a, const Path& t) -> bool {
                if (!a.asg || !t.sym) return false;
                bool sawMatch = false;
                for (const auto& acc : procFlow->accesses) {
                    if (!acc.write || acc.asg != a.asg || acc.sym != t.sym) continue;
                    if (acc.range.second < t.lo || acc.range.first > t.hi) continue;
                    sawMatch = true;
                    if (!acc.exact || acc.live || acc.used || acc.observed || acc.exposed) return false;
                }
                return sawMatch;
            };
            std::unordered_set<Model::DeadEdgeKey, Model::DeadEdgeHash> procLiveEdges;
            curLiveProcEdges = &procLiveEdges;
            struct LiveEdgesGuard { Builder& b; ~LiveEdgesGuard() { b.curLiveProcEdges = nullptr; b.curDeadWrite = false; } } liveGuard{*this};
            std::vector<std::pair<const Assign*, std::vector<Path>>> deferredDead;
            curDeadWrite = false;
            for (auto& a : assigns) {
                std::vector<Path> liveTargets, deadTargets;
                for (auto& t : a.targets) {
                    if (isDeadWrite(a, t)) deadTargets.push_back(t);
                    else liveTargets.push_back(t);
                }
                if (!liveTargets.empty()) emitAssign(ctx, a, liveTargets, EdgeType::Comb, proc, pb.location);
                if (!deadTargets.empty()) deferredDead.push_back({&a, std::move(deadTargets)});
            }
            curDeadWrite = true;
            for (const auto& [a, deadTargets] : deferredDead)
                emitAssign(ctx, *a, deadTargets, EdgeType::Comb, proc, pb.location);
            curDeadWrite = false;
            if (kind == ProcKind::Comb) analyseCombProcedure(pb, ctx, ec, proc);
            return;
        }
        m.numSeqProcedures++;
        StuckBodyChecker stuckCheck;
        stuckCheck.outerTimed = v.outerTimed;
        body.visit(stuckCheck);
        analyseSequential(pb, ctx, ec, events, first, assigns, proc, v.hasBodyTiming || stuckCheck.unsafe, v.hasStmtTiming);
    }

    void analyseCombProcedure(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx, EvalContext& ec, uint32_t proc) {
        using ArmList = std::vector<CombQualifierArm>;
        using CombState = std::unordered_map<NodeId, ArmList>;
        constexpr size_t kCombUnrollBudget = 2048;
        struct CombWalker : ASTVisitor<CombWalker, VisitFlags::AllGood> {
            Builder& b;
            EvalContext& ec;
            const InstanceSymbol* ctx;
            CombState state;
            std::unordered_set<NodeId> guardReads;
            std::unordered_set<NodeId> anyReads;
            std::unordered_set<NodeId> staleNodes;
            bool aborted = false;
            bool staleGuardRead = false;
            bool incompleteTiming = false;
            size_t unrollWalks = 0, unrollFactor = 1;

            CombWalker(Builder& b, EvalContext& ec, const InstanceSymbol* ctx) : b(b), ec(ec), ctx(ctx) {}

            void noteDataReads(const std::vector<NodeId>& deps) {
                for (NodeId d : deps) {
                    anyReads.insert(d);
                    if (auto it = state.find(d); it != state.end()) {
                        for (const auto& a : it->second)
                            if (a.unassigned) staleNodes.insert(d);
                    }
                }
            }

            void noteGuardDeps(const std::vector<NodeId>& deps) {
                for (NodeId d : deps) {
                    anyReads.insert(d);
                    guardReads.insert(d);
                    if (staleNodes.count(d)) staleGuardRead = true;
                    if (auto it = state.find(d); it != state.end()) {
                        for (const auto& a : it->second) {
                            if (a.unassigned) staleGuardRead = true;
                            if (a.dataDependencies) {
                                for (NodeId sd : *a.dataDependencies) {
                                    guardReads.insert(sd);
                                    if (staleNodes.count(sd)) staleGuardRead = true;
                                }
                            }
                            if (a.controlDependencies) {
                                for (NodeId sc : *a.controlDependencies) {
                                    guardReads.insert(sc);
                                    if (staleNodes.count(sc)) staleGuardRead = true;
                                }
                            }
                        }
                    }
                }
            }

            void assignState(NodeId n, ArmList arms) {
                if (guardReads.count(n)) staleGuardRead = true;
                if (anyReads.count(n)) staleNodes.insert(n);
                state[n] = std::move(arms);
            }

            static CombQualifierArm unassignedArm() {
                CombQualifierArm arm;
                arm.unassigned = true;
                arm.dataDependencies = std::make_shared<const std::vector<NodeId>>();
                arm.controlDependencies = std::make_shared<const std::vector<NodeId>>();
                return arm;
            }

            // Both lists are usually sorted already (every result of this is): merge them in linear time instead of sorting again.
            static void mergeUnique(std::vector<NodeId>& dst, const std::vector<NodeId>& src) {
                const auto mid = static_cast<std::ptrdiff_t>(dst.size());
                dst.insert(dst.end(), src.begin(), src.end());
                if (std::is_sorted(dst.begin(), dst.begin() + mid) && std::is_sorted(dst.begin() + mid, dst.end()))
                    std::inplace_merge(dst.begin(), dst.begin() + mid, dst.end());
                else
                    std::sort(dst.begin(), dst.end());
                dst.erase(std::unique(dst.begin(), dst.end()), dst.end());
            }

            static ArmList attachGuard(const ArmList& arms, const GuardCondition& guard,
                                       bool condApprox, bool condUncertain) {
                ArmList out;
                out.reserve(arms.size());
                for (auto arm : arms) {
                    arm.guards.insert(arm.guards.begin(), guard);
                    auto ctrl = std::make_shared<std::vector<NodeId>>(guard.dependencies);
                    if (arm.controlDependencies) mergeUnique(*ctrl, *arm.controlDependencies);
                    arm.controlDependencies = std::move(ctrl);
                    arm.controlApproximate |= condApprox;
                    arm.uncertainCall |= condUncertain;
                    out.push_back(std::move(arm));
                }
                return out;
            }

            static ArmList capArms(ArmList arms, bool& collapsed) {
                if (arms.size() <= kMaxQualifierArmsPerBit) return arms;
                collapsed = true;
                CombQualifierArm fallback;
                auto data = std::make_shared<std::vector<NodeId>>();
                auto ctrl = std::make_shared<std::vector<NodeId>>();
                for (const auto& arm : arms) {
                    if (arm.dataDependencies) mergeUnique(*data, *arm.dataDependencies);
                    if (arm.controlDependencies) mergeUnique(*ctrl, *arm.controlDependencies);
                    fallback.dataApproximate |= arm.dataApproximate;
                    fallback.controlApproximate |= arm.controlApproximate;
                    fallback.uncertainCall |= arm.uncertainCall;
                    fallback.unassigned |= arm.unassigned;
                }
                fallback.dataApproximate = true;
                fallback.dataDependencies = std::move(data);
                fallback.controlDependencies = std::move(ctrl);
                if (!fallback.controlDependencies->empty()) {
                    std::vector<BoolFactPtr> refs;
                    for (NodeId n : *fallback.controlDependencies)
                        refs.push_back(std::make_shared<BoolFact>(BoolFact{BoolFact::Kind::Node, n, false, false, {}}));
                    fallback.guards.push_back({std::make_shared<BoolFact>(BoolFact{BoolFact::Kind::Unknown, kNoNode, false, fallback.uncertainCall, std::move(refs)}),
                                               *fallback.controlDependencies});
                }
                return {std::move(fallback)};
            }

            ArmList resolveArmAgainstState(const CombQualifierArm& arm, const CombState& snapshot) {
                if (!arm.dataDependencies || arm.dataDependencies->empty()) return {arm};
                std::vector<NodeId> stateDeps;
                for (NodeId d : *arm.dataDependencies)
                    if (snapshot.count(d)) stateDeps.push_back(d);
                if (stateDeps.empty()) return {arm};
                if (arm.dataDependencies->size() == 1 && stateDeps.size() == 1) {
                    const auto& srcArms = snapshot.at(stateDeps[0]);
                    ArmList out;
                    for (const auto& srcArm : srcArms) {
                        CombQualifierArm merged = arm;
                        merged.guards.insert(merged.guards.end(), srcArm.guards.begin(), srcArm.guards.end());
                        auto ctrl = std::make_shared<std::vector<NodeId>>();
                        if (arm.controlDependencies) mergeUnique(*ctrl, *arm.controlDependencies);
                        if (srcArm.controlDependencies) mergeUnique(*ctrl, *srcArm.controlDependencies);
                        merged.controlDependencies = std::move(ctrl);
                        merged.dataDependencies = srcArm.dataDependencies;
                        merged.dataApproximate |= srcArm.dataApproximate;
                        merged.controlApproximate |= srcArm.controlApproximate;
                        merged.uncertainCall |= srcArm.uncertainCall;
                        merged.unassigned |= srcArm.unassigned;
                        out.push_back(std::move(merged));
                    }
                    bool dummy = false;
                    return capArms(std::move(out), dummy);
                }
                // Multiple data reads where at least one was already assigned in this procedure.
                ArmList cur{arm};
                for (NodeId sd : stateDeps) {
                    const auto& srcArms = snapshot.at(sd);
                    if (cur.size() * srcArms.size() > kMaxQualifierArmsPerBit) {
                        // Conservative union of dependencies for this read.
                        for (auto& c : cur) {
                            auto data = std::make_shared<std::vector<NodeId>>();
                            if (c.dataDependencies) {
                                for (NodeId d : *c.dataDependencies)
                                    if (d != sd) data->push_back(d);
                            }
                            auto ctrl = std::make_shared<std::vector<NodeId>>();
                            if (c.controlDependencies) mergeUnique(*ctrl, *c.controlDependencies);
                            for (const auto& sa : srcArms) {
                                if (sa.dataDependencies) mergeUnique(*data, *sa.dataDependencies);
                                if (sa.controlDependencies) mergeUnique(*ctrl, *sa.controlDependencies);
                                c.dataApproximate |= sa.dataApproximate;
                                c.controlApproximate |= sa.controlApproximate;
                                c.uncertainCall |= sa.uncertainCall;
                                c.unassigned |= sa.unassigned;
                            }
                            std::sort(data->begin(), data->end());
                            data->erase(std::unique(data->begin(), data->end()), data->end());
                            c.dataDependencies = std::move(data);
                            c.controlDependencies = std::move(ctrl);
                            c.dataApproximate = true;
                        }
                        continue;
                    }
                    ArmList next;
                    for (const auto& c : cur) {
                        for (const auto& sa : srcArms) {
                            CombQualifierArm merged = c;
                            merged.guards.insert(merged.guards.end(), sa.guards.begin(), sa.guards.end());
                            auto data = std::make_shared<std::vector<NodeId>>();
                            if (c.dataDependencies) {
                                for (NodeId d : *c.dataDependencies)
                                    if (d != sd) data->push_back(d);
                            }
                            if (sa.dataDependencies) mergeUnique(*data, *sa.dataDependencies);
                            auto ctrl = std::make_shared<std::vector<NodeId>>();
                            if (c.controlDependencies) mergeUnique(*ctrl, *c.controlDependencies);
                            if (sa.controlDependencies) mergeUnique(*ctrl, *sa.controlDependencies);
                            merged.dataDependencies = std::move(data);
                            merged.controlDependencies = std::move(ctrl);
                            merged.dataApproximate |= sa.dataApproximate;
                            merged.controlApproximate |= sa.controlApproximate;
                            merged.uncertainCall |= sa.uncertainCall;
                            merged.unassigned |= sa.unassigned;
                            next.push_back(std::move(merged));
                        }
                    }
                    cur = std::move(next);
                }
                bool dummy = false;
                return capArms(std::move(cur), dummy);
            }

            void markFallbackTargets(const Expression& lhs, const Expression& rhs) {
                auto data = std::make_shared<const std::vector<NodeId>>(b.nodesOfExpr(rhs, ec, ctx));
                auto ctrl = std::make_shared<const std::vector<NodeId>>();
                bool approx = true;
                bool uncertain = !b.callIssues(rhs, ctx).empty() || !b.callIssues(lhs, ctx).empty();
                CombQualifierArm arm{{}, data, ctrl, approx, approx, uncertain, false};
                for (const auto& p : b.pathsOf(lhs, ec, true)) {
                    for (NodeId n : b.nodesFor(ctx, p)) {
                        assignState(n, resolveArmAgainstState(arm, state));
                    }
                }
            }

            void handle(const TimedStatement& t) {
                if (t.timing.kind != TimingControlKind::ImplicitEvent) incompleteTiming = true;
                t.stmt.visit(*this);
            }
            void handle(const BlockStatement& blk) {
                if (blk.blockKind != StatementBlockKind::Sequential) {
                    aborted = true;
                    return;
                }
                blk.body.visit(*this);
            }
            void handle(const VariableDeclStatement& vd) {
                const auto& sym = vd.symbol;
                if (!isGraphSymbol(sym) || sym.getType().isUnpackedArray()) return;
                const auto* init = sym.getInitializer();
                if (!init) return;
                init->visit(*this);
                uint64_t w = widthOf(sym);
                if (!w || b.collapse(sym, w)) return;
                auto arms = b.decomposeExprArms(*init, ec, ctx, w);
                const CombState snapshot = state;
                for (size_t i = 0; i < w; ++i) {
                    NodeId n = b.nodeForBit(ctx, {&sym, static_cast<uint32_t>(w - 1 - i)});
                    ArmList resolved;
                    for (const auto& arm : arms[i]) {
                        if (arm.dataDependencies) noteDataReads(*arm.dataDependencies);
                        for (const auto& g : arm.guards) noteGuardDeps(g.dependencies);
                        auto expanded = resolveArmAgainstState(arm, snapshot);
                        resolved.insert(resolved.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
                    }
                    bool dummy = false;
                    assignState(n, capArms(std::move(resolved), dummy));
                }
            }
            void handle(const CallExpression& c) {
                if (c.isSystemCall()) {
                    const auto& name = std::get<CallExpression::SystemCallInfo>(c.subroutine).subroutine->name;
                    static const std::set<std::string_view> mutating = {
                        "delete", "push_back", "push_front", "pop_back", "pop_front",
                        "insert", "shuffle", "reverse", "sort", "rsort", "putc", "randomize"};
                    if (c.hasOutputArgs() || mutating.count(name)) aborted = true;
                    visitDefault(c);
                    return;
                }
                const auto& summary = b.summaryFor(c);
                const auto* sub = std::get<const SubroutineSymbol*>(c.subroutine);
                const bool hasOutFormal = sub && std::any_of(sub->getArguments().begin(), sub->getArguments().end(),
                    [](const auto* arg) { return arg->direction != ArgumentDirection::In; });
                if (!summary.writes.empty() || summary.unboundedWrites || c.thisClass() || c.hasOutputArgs() || hasOutFormal)
                    aborted = true;
                visitDefault(c);
            }
            void handle(const UnaryExpression& u) {
                if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                    u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) {
                    markFallbackTargets(u.operand(), u.operand());
                }
                visitDefault(u);
            }
            void handle(const AssignmentExpression& a) {
                if (a.isLValueArg()) {
                    visitDefault(a);
                    return;
                }
                a.left().visit(*this);
                a.right().visit(*this);
                if (a.isNonBlocking() || a.isCompound() || !b.callsIn(a.left()).empty() || b.hasDynamicSelect(a.left(), ec)) {
                    markFallbackTargets(a.left(), a.right());
                    return;
                }
                std::vector<BitRef> targetBits;
                if (!b.bitList(a.left(), ec, targetBits) || targetBits.empty() ||
                    !std::all_of(targetBits.begin(), targetBits.end(), [](const BitRef& bit) { return bit.sym != nullptr && !bit.sym->getType().isUnpackedArray(); }) ||
                    b.collapse(*targetBits.front().sym, widthOf(*targetBits.front().sym))) {
                    markFallbackTargets(a.left(), a.right());
                    return;
                }
                auto arms = b.decomposeExprArms(a.right(), ec, ctx, targetBits.size());
                if (arms.size() != targetBits.size()) {
                    markFallbackTargets(a.left(), a.right());
                    return;
                }
                const CombState snapshot = state;
                for (size_t i = 0; i < targetBits.size(); ++i) {
                    NodeId targetNode = b.nodeForBit(ctx, targetBits[i]);
                    ArmList resolved;
                    for (const auto& arm : arms[i]) {
                        if (arm.dataDependencies) noteDataReads(*arm.dataDependencies);
                        for (const auto& g : arm.guards) noteGuardDeps(g.dependencies);
                        auto expanded = resolveArmAgainstState(arm, snapshot);
                        resolved.insert(resolved.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
                    }
                    bool dummy = false;
                    assignState(targetNode, capArms(std::move(resolved), dummy));
                }
            }
            static bool sameBoolFact(const BoolFactPtr& a, const BoolFactPtr& b) {
                if (a.get() == b.get()) return a != nullptr;
                if (!a || !b || a->kind != b->kind) return false;
                if (a->kind == BoolFact::Kind::Unknown) return false;
                if (a->node != b->node || a->value != b->value || a->opaque != b->opaque ||
                    a->args.size() != b->args.size())
                    return false;
                for (size_t i = 0; i < a->args.size(); ++i)
                    if (!sameBoolFact(a->args[i], b->args[i])) return false;
                return true;
            }
            static bool sameGuard(const GuardCondition& a, const GuardCondition& b) {
                return a.dependencies == b.dependencies && sameBoolFact(a.predicate, b.predicate);
            }
            static bool sameArms(const ArmList& a, const ArmList& c) {
                if (a.size() != c.size()) return false;
                for (size_t i = 0; i < a.size(); ++i) {
                    if (a[i].unassigned != c[i].unassigned ||
                        a[i].dataApproximate != c[i].dataApproximate ||
                        a[i].controlApproximate != c[i].controlApproximate ||
                        a[i].uncertainCall != c[i].uncertainCall ||
                        a[i].guards.size() != c[i].guards.size())
                        return false;
                    const bool sameData = a[i].dataDependencies == c[i].dataDependencies ||
                        (a[i].dataDependencies && c[i].dataDependencies && *a[i].dataDependencies == *c[i].dataDependencies);
                    if (!sameData) return false;
                    const bool sameCtrl = a[i].controlDependencies == c[i].controlDependencies ||
                        (a[i].controlDependencies && c[i].controlDependencies && *a[i].controlDependencies == *c[i].controlDependencies);
                    if (!sameCtrl) return false;
                    for (size_t j = 0; j < a[i].guards.size(); ++j)
                        if (!sameGuard(a[i].guards[j], c[i].guards[j])) return false;
                }
                return true;
            }
            void handle(const ConditionalStatement& s) {
                if (b.design.hasCaseMode() && s.conditions.size() == 1 && !s.conditions[0].pattern) {
                    int v = b.modeValue(*s.conditions[0].expr, ec);
                    if (v >= 0) {
                        if (v) s.ifTrue.visit(*this);
                        else if (s.ifFalse) s.ifFalse->visit(*this);
                        return;
                    }
                }
                if (s.conditions.size() == 1 && !s.conditions[0].pattern) {
                    if (auto value = model::elaboratedCondition(*s.conditions[0].expr, ec)) {
                        if (*value) s.ifTrue.visit(*this);
                        else if (s.ifFalse) s.ifFalse->visit(*this);
                        return;
                    }
                }
                if (s.conditions.size() == 1 && !s.conditions[0].pattern && b.alwaysFalseExpr(*s.conditions[0].expr, ec)) {
                    if (s.ifFalse) s.ifFalse->visit(*this);
                    return;
                }
                for (const auto& c : s.conditions) c.expr->visit(*this);
                if (s.conditions.size() != 1 || s.conditions[0].pattern) {
                    // Unsupported condition pattern: conservatively merge under Unknown guard.
                    std::vector<NodeId> deps;
                    BoolFactPtr pred = b.unknownBoolean(*s.conditions[0].expr, ec, ctx);
                    for (const auto& c : s.conditions) mergeUnique(deps, b.nodesOfExpr(*c.expr, ec, ctx));
                    GuardCondition g{pred, deps};
                    noteGuardDeps(g.dependencies);
                    const CombState entry = state;
                    CombWalker trueW(b, ec, ctx), falseW(b, ec, ctx);
                    trueW.state = entry; falseW.state = entry;
                    trueW.guardReads = guardReads; falseW.guardReads = guardReads;
                    trueW.anyReads = anyReads; falseW.anyReads = anyReads;
                    trueW.staleNodes = staleNodes; falseW.staleNodes = staleNodes;
                    s.ifTrue.visit(trueW);
                    if (s.ifFalse) s.ifFalse->visit(falseW);
                    aborted |= trueW.aborted || falseW.aborted;
                    staleGuardRead |= trueW.staleGuardRead || falseW.staleGuardRead;
                    incompleteTiming |= trueW.incompleteTiming || falseW.incompleteTiming;
                    guardReads.insert(trueW.guardReads.begin(), trueW.guardReads.end());
                    guardReads.insert(falseW.guardReads.begin(), falseW.guardReads.end());
                    anyReads.insert(trueW.anyReads.begin(), trueW.anyReads.end());
                    anyReads.insert(falseW.anyReads.begin(), falseW.anyReads.end());
                    staleNodes.insert(trueW.staleNodes.begin(), trueW.staleNodes.end());
                    staleNodes.insert(falseW.staleNodes.begin(), falseW.staleNodes.end());
                    std::unordered_set<NodeId> touched;
                    for (const auto& [n, arms] : trueW.state) {
                        auto eit = entry.find(n);
                        if (eit == entry.end() || !sameArms(arms, eit->second)) touched.insert(n);
                    }
                    for (const auto& [n, arms] : falseW.state) {
                        auto eit = entry.find(n);
                        if (eit == entry.end() || !sameArms(arms, eit->second)) touched.insert(n);
                    }
                    for (NodeId n : touched) {
                        ArmList merged;
                        auto tIt = trueW.state.find(n);
                        ArmList tArms = tIt != trueW.state.end() ? tIt->second : ArmList{unassignedArm()};
                        auto fIt = falseW.state.find(n);
                        ArmList fArms = fIt != falseW.state.end() ? fIt->second : ArmList{unassignedArm()};
                        for (auto& a : attachGuard(tArms, g, false, false)) merged.push_back(std::move(a));
                        for (auto& a : attachGuard(fArms, g, false, false)) merged.push_back(std::move(a));
                        bool dummy = false;
                        state[n] = capArms(std::move(merged), dummy);
                    }
                    return;
                }
                const Expression& condExpr = *s.conditions[0].expr;
                GuardCondition trueGuard{b.booleanOf(condExpr, ec, ctx), b.nodesOfExpr(condExpr, ec, ctx)};
                GuardCondition falseGuard{b.boolFact(BoolFact::Kind::Not, {trueGuard.predicate}), trueGuard.dependencies};
                noteGuardDeps(trueGuard.dependencies);
                const bool condApprox = b.hasDynamicSelect(condExpr, ec) || !b.callsIn(condExpr).empty();
                const bool condUncertain = !b.callIssues(condExpr, ctx).empty();
                const CombState entry = state;
                CombWalker trueW(b, ec, ctx);
                trueW.state = entry;
                trueW.guardReads = guardReads;
                trueW.anyReads = anyReads;
                trueW.staleNodes = staleNodes;
                trueW.unrollWalks = unrollWalks;
                trueW.unrollFactor = unrollFactor;
                s.ifTrue.visit(trueW);
                CombState falseState = entry;
                if (s.ifFalse) {
                    CombWalker falseW(b, ec, ctx);
                    falseW.state = entry;
                    falseW.guardReads = guardReads;
                    falseW.anyReads = anyReads;
                    falseW.staleNodes = staleNodes;
                    falseW.unrollWalks = trueW.unrollWalks;
                    falseW.unrollFactor = unrollFactor;
                    s.ifFalse->visit(falseW);
                    falseState = std::move(falseW.state);
                    aborted |= falseW.aborted;
                    staleGuardRead |= falseW.staleGuardRead;
                    incompleteTiming |= falseW.incompleteTiming;
                    guardReads.insert(falseW.guardReads.begin(), falseW.guardReads.end());
                    anyReads.insert(falseW.anyReads.begin(), falseW.anyReads.end());
                    staleNodes.insert(falseW.staleNodes.begin(), falseW.staleNodes.end());
                    unrollWalks = falseW.unrollWalks;
                } else {
                    unrollWalks = trueW.unrollWalks;
                }
                aborted |= trueW.aborted;
                staleGuardRead |= trueW.staleGuardRead;
                incompleteTiming |= trueW.incompleteTiming;
                guardReads.insert(trueW.guardReads.begin(), trueW.guardReads.end());
                anyReads.insert(trueW.anyReads.begin(), trueW.anyReads.end());
                staleNodes.insert(trueW.staleNodes.begin(), trueW.staleNodes.end());
                std::unordered_set<NodeId> touched;
                for (const auto& [n, arms] : trueW.state) {
                    auto eit = entry.find(n);
                    if (eit == entry.end() || !sameArms(arms, eit->second)) touched.insert(n);
                }
                for (const auto& [n, arms] : falseState) {
                    auto eit = entry.find(n);
                    if (eit == entry.end() || !sameArms(arms, eit->second)) touched.insert(n);
                }
                for (NodeId n : touched) {
                    ArmList merged;
                    auto tIt = trueW.state.find(n);
                    ArmList tArms = tIt != trueW.state.end() ? tIt->second : ArmList{unassignedArm()};
                    auto fIt = falseState.find(n);
                    ArmList fArms = fIt != falseState.end() ? fIt->second : ArmList{unassignedArm()};
                    for (auto& a : attachGuard(tArms, trueGuard, condApprox, condUncertain)) merged.push_back(std::move(a));
                    for (auto& a : attachGuard(fArms, falseGuard, condApprox, condUncertain)) merged.push_back(std::move(a));
                    bool dummy = false;
                    state[n] = capArms(std::move(merged), dummy);
                }
            }
            void handle(const CaseStatement& s) {
                s.expr.visit(*this);
                std::vector<NodeId> caseDeps = b.nodesOfExpr(s.expr, ec, ctx);
                bool caseUncertain = !b.callIssues(s.expr, ctx).empty();
                bool caseApprox = b.hasDynamicSelect(s.expr, ec) || !b.callsIn(s.expr).empty();
                for (const auto& g : s.items)
                    for (const auto* e : g.expressions) {
                        e->visit(*this);
                        mergeUnique(caseDeps, b.nodesOfExpr(*e, ec, ctx));
                        caseUncertain |= !b.callIssues(*e, ctx).empty();
                        caseApprox |= b.hasDynamicSelect(*e, ec) || !b.callsIn(*e).empty();
                    }
                GuardCondition caseGuard{b.unknownBoolean(s.expr, ec, ctx), caseDeps};
                noteGuardDeps(caseGuard.dependencies);
                const CombState entry = state;
                std::vector<CombState> armStates;
                for (const auto& g : s.items) {
                    CombWalker w(b, ec, ctx);
                    w.state = entry;
                    w.guardReads = guardReads;
                    w.anyReads = anyReads;
                    w.staleNodes = staleNodes;
                    g.stmt->visit(w);
                    aborted |= w.aborted;
                    staleGuardRead |= w.staleGuardRead;
                    incompleteTiming |= w.incompleteTiming;
                    guardReads.insert(w.guardReads.begin(), w.guardReads.end());
                    anyReads.insert(w.anyReads.begin(), w.anyReads.end());
                    staleNodes.insert(w.staleNodes.begin(), w.staleNodes.end());
                    armStates.push_back(std::move(w.state));
                }
                if (s.defaultCase) {
                    CombWalker w(b, ec, ctx);
                    w.state = entry;
                    w.guardReads = guardReads;
                    w.anyReads = anyReads;
                    w.staleNodes = staleNodes;
                    s.defaultCase->visit(w);
                    aborted |= w.aborted;
                    staleGuardRead |= w.staleGuardRead;
                    incompleteTiming |= w.incompleteTiming;
                    guardReads.insert(w.guardReads.begin(), w.guardReads.end());
                    anyReads.insert(w.anyReads.begin(), w.anyReads.end());
                    staleNodes.insert(w.staleNodes.begin(), w.staleNodes.end());
                    armStates.push_back(std::move(w.state));
                } else {
                    armStates.push_back(entry);
                }
                std::unordered_set<NodeId> touched;
                for (const auto& as : armStates)
                    for (const auto& [n, _] : as) touched.insert(n);
                for (NodeId n : touched) {
                    ArmList merged;
                    for (const auto& as : armStates) {
                        auto it = as.find(n);
                        ArmList arms = it != as.end() ? it->second : ArmList{unassignedArm()};
                        for (auto& a : attachGuard(arms, caseGuard, caseApprox, caseUncertain)) merged.push_back(std::move(a));
                    }
                    bool dummy = false;
                    state[n] = capArms(std::move(merged), dummy);
                }
            }
            void handle(const ForLoopStatement& s) {
                std::vector<LoopBinding> iterations;
                if (b.unrollForLoop(s, ec, iterations) && iterations.size() * unrollFactor + unrollWalks <= kCombUnrollBudget) {
                    unrollWalks += iterations.size() * unrollFactor;
                    const size_t savedFactor = unrollFactor;
                    unrollFactor *= std::max<size_t>(1, iterations.size());
                    const auto saved = b.boundLoops;
                    for (const auto& binding : iterations) {
                        for (size_t k = saved.size(); k < binding.size(); ++k) ec.createLocal(binding[k].first, binding[k].second);
                        b.boundLoops = binding;
                        s.body.visit(*this);
                        for (size_t k = saved.size(); k < binding.size(); ++k) ec.deleteLocal(binding[k].first);
                    }
                    b.boundLoops = saved;
                    unrollFactor = savedFactor;
                    return;
                }
                aborted = true;
            }
            void handle(const WhileLoopStatement&) { aborted = true; }
            void handle(const DoWhileLoopStatement&) { aborted = true; }
            void handle(const ReturnStatement&) { aborted = true; }
            void handle(const DisableStatement&) { aborted = true; }
            void handle(const BreakStatement&) { aborted = true; }
            void handle(const ContinueStatement&) { aborted = true; }
        };

        JumpFinder jumps;
        pb.getBody().visit(jumps);
        CombWalker walker(*this, ec, ctx);
        pb.getBody().visit(walker);
        if (jumps.found || walker.aborted || walker.staleGuardRead) {
            // Unstructured control flow / un-unrolled loop: mark all written targets incomplete/unsupported.
            std::vector<Assign> raw;
            StmtVisitor sv(*this, ec, raw, ctx);
            sv.modelSteps = true;
            pb.getBody().visit(sv);
            for (const auto& a : raw) {
                for (const auto& t : a.targets)
                    for (NodeId n : nodesFor(ctx, t)) {
                        m.booleanDrivers[n] = {BooleanDriver{boolFact(BoolFact::Kind::Unknown, {}), true}};
                        CombQualifierSummary s;
                        s.supported = false;
                        s.incomplete = true;
                        recordCombDriver(n, proc, std::move(s));
                    }
            }
            return;
        }
        auto dependsOnStale = [&](const CombQualifierArm& arm) {
            if (arm.dataDependencies)
                for (NodeId d : *arm.dataDependencies)
                    if (walker.staleNodes.count(d)) return true;
            if (arm.controlDependencies)
                for (NodeId c : *arm.controlDependencies)
                    if (walker.staleNodes.count(c)) return true;
            return false;
        };
        for (auto& [node, arms] : walker.state) {
            bool staleValue = walker.staleNodes.count(node) != 0;
            CombQualifierSummary summary;
            summary.supported = true;
            summary.incomplete = walker.incompleteTiming;
            for (auto& arm : arms) {
                if (dependsOnStale(arm)) staleValue = true;
                if (arm.unassigned) {
                    summary.incomplete = true;
                    staleValue = true;
                } else summary.arms.push_back(std::move(arm));
            }
            if (staleValue) {
                m.booleanDrivers[node] = {BooleanDriver{boolFact(BoolFact::Kind::Unknown, {}), true}};
                summary.supported = false;
                summary.incomplete = true;
            }
            recordCombDriver(node, proc, std::move(summary));
        }
    }

    void analyseSequential(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx, EvalContext& ec,
                           const std::vector<Event>& events, const Statement* first, std::vector<Assign>& assigns,
                           uint32_t proc, bool procHasBodyTiming = false, bool procHasStmtTiming = false) {
        // Expands condition paths through exact single-driver copies (`plainCopyOf`), and also through multiply-driven
        // aliases (`multiDrivenSyms`) while reporting that the guard passed through a multiply-driven net (round 4 A2a).
        auto expandCondPaths = [&](const Expression& e, std::set<Path>& out, bool* sawMultiDriven = nullptr) {
            for (const auto& p : pathsOf(e, ec, false)) {
                const Path cp = canonPath(p);
                if (cp.sym && multiDrivenSyms.count(cp.sym)) {
                    if (sawMultiDriven) *sawMultiDriven = true;
                    if (auto it = multiDriverSources.find(cp.sym); it != multiDriverSources.end()) {
                        for (const ValueSymbol* src : it->second) {
                            if (widthOf(*src) == widthOf(*cp.sym)) out.insert(canonPath(Path{src, cp.lo, cp.hi}));
                        }
                    }
                    out.insert(cp);
                } else {
                    out.insert(cp);
                }
            }
        };
        // Every signal a condition reads anywhere in a block (if tests, case selectors and items), whatever its nesting.
        struct BlockConditions : ASTVisitor<BlockConditions, VisitFlags::AllGood> {
            decltype(expandCondPaths)& expand; std::set<Path>& out;
            BlockConditions(decltype(expandCondPaths)& expand, std::set<Path>& out) : expand(expand), out(out) {}
            void note(const Expression& e) { expand(e, out, nullptr); }
            void handle(const ConditionalStatement& s) { for (const auto& c : s.conditions) note(*c.expr); visitDefault(s); }
            void handle(const ConditionalExpression& e) { for (const auto& c : e.conditions) note(*c.expr); visitDefault(e); }
            void handle(const CaseStatement& s) {
                note(s.expr);
                for (const auto& g : s.items) for (const auto* e : g.expressions) note(*e);
                visitDefault(s);
            }
            void handle(const PatternCaseStatement& s) { note(s.expr); visitDefault(s); }
        };
        // Target bits written under a condition nested in a branch (a test inside `if (!rst_n) begin if (en) v[1] <= 0; end`)
        struct NestedWriteTargets : ASTVisitor<NestedWriteTargets, VisitFlags::AllGood> {
            Builder& b; EvalContext& ec; const InstanceSymbol* ctx; std::set<NodeId>& out; int depth = 0;
            NestedWriteTargets(Builder& b, EvalContext& ec, const InstanceSymbol* ctx, std::set<NodeId>& out)
                : b(b), ec(ec), ctx(ctx), out(out) {}
            void handle(const ConditionalStatement& s) { depth++; visitDefault(s); depth--; }
            void handle(const CaseStatement& s) { depth++; visitDefault(s); depth--; }
            void handle(const PatternCaseStatement& s) { depth++; visitDefault(s); depth--; }
            void handle(const AssignmentExpression& a) {
                if (depth > 0) {
                    for (const auto& p : b.pathsOf(a.left(), ec, true))
                        for (NodeId n : b.nodesFor(ctx, p)) out.insert(n);
                }
                visitDefault(a);
            }
        };
        // --- identify clock and asynchronous resets among the edge events
        std::vector<const Event*> edgeEvents;
        for (auto& e : events) if (e.edge) edgeEvents.push_back(&e);
        const Statement* inner = first && first->kind == StatementKind::Timed ? firstMeaningful(&first->as<TimedStatement>().stmt) : nullptr;
        std::set<Path> topCond;   // canonical paths (a copy of a net counts as the net)
        const ConditionalStatement* topIf = nullptr;
        if (inner && inner->kind == StatementKind::Conditional) {
            topIf = &inner->as<ConditionalStatement>();
            // the whole if / else-if chain: `if (!rst_n) ... else if (!set_n) ...` tests two asynchronous controls
            for (const Statement* cur = topIf; cur && cur->kind == StatementKind::Conditional; ) {
                auto& cs = cur->as<ConditionalStatement>();
                for (auto& c : cs.conditions) expandCondPaths(*c.expr, topCond);
                cur = cs.ifFalse ? firstMeaningful(cs.ifFalse) : nullptr;
            }
        }
        // `case (rst_n) ...` at the top of the procedure tests its asynchronous controls too; the arms' forced values are not analysed
        const CaseStatement* topCase = nullptr;
        if (inner && inner->kind == StatementKind::Case) {
            topCase = &inner->as<CaseStatement>();
            expandCondPaths(topCase->expr, topCond);
        }
        const Event* clk = nullptr;
        bool dualEdge = false;
        std::vector<const Event*> rsts;
        bool unanalysedControl = false;   // an asynchronous control found only by the block's conditions: its forced value is not analysed
        if (edgeEvents.size() == 1) {
            clk = edgeEvents[0];
        } else {
            std::vector<const Event*> nonReset;
            for (auto* e : edgeEvents) {
                if (e->ok && topCond.count(canonPath(e->path))) rsts.push_back(e); else nonReset.push_back(e);
            }
            if (nonReset.size() == 1) clk = nonReset[0];
            else if (nonReset.size() == 2) {
                const auto* a = nonReset[0];
                const auto* b = nonReset[1];
                std::vector<BitRef> aBits, bBits;
                const bool opposite = (a->edge == 1 && b->edge == 2) || (a->edge == 2 && b->edge == 1);
                if (a->ok && b->ok && opposite && a->path == b->path &&
                    bitList(*a->expression, ec, aBits) && bitList(*b->expression, ec, bBits) &&
                    aBits.size() == 1 && bBits.size() == 1 && aBits[0].sym &&
                    aBits[0].sym == bBits[0].sym && aBits[0].bit == bBits[0].bit) {
                    clk = a;
                    dualEdge = true;
                }
            }
            // No top-level test decides it (`if (!rst_n) ... else case (set_n)`, `case (1'b1) !rst_n: ...`, `q <= rst_n ? d : 0`): the
            // only edge that no condition anywhere in the block reads is the clock. Every other edge stays an asynchronous control.
            if (!clk && first) {
                std::set<Path> tested;
                BlockConditions collect(expandCondPaths, tested);
                first->visit(collect);
                std::vector<const Event*> untested;
                for (auto* e : edgeEvents) if (e->ok && !tested.count(canonPath(e->path))) untested.push_back(e);
                if (untested.size() == 1) clk = untested[0];
            }
            if (clk) {
                // every edge but the clock is a control; a dual-edge pair of the clock itself is one clock
                rsts.clear();
                for (auto* e : edgeEvents)
                    if (e != clk && !(dualEdge && e->path == clk->path)) rsts.push_back(e);
            } else {
                // ambiguous: cannot tell which edge is the clock. When a top-level test names an asynchronous control, that control keeps
                // its known force and every other edge stays an asynchronous control of unknown force (round 4 A1, a clock copy beside
                // extra events). With no tested control no edge is listed (two clocks are not two resets). Either way the block is
                // reported as an unresolved clock below.
                rsts.clear();
                bool anyTested = false;
                for (auto* e : edgeEvents) anyTested = anyTested || (e->ok && topCond.count(canonPath(e->path)) > 0);
                if (anyTested) for (auto* e : edgeEvents) if (e->ok) rsts.push_back(e);
            }
        }
        NodeId clockNode = kNoNode;
        int clockEdge = 0;
        bool clockOk = clk && clk->ok && edgeEvents.size() >= 1;
        if (clockOk) {
            auto ns = nodesFor(ctx, clk->path);
            clockNode = ns.empty() ? kNoNode : ns[0];
            clockEdge = dualEdge ? 3 : clk->edge;
            if (clk->path.hi != clk->path.lo && widthOf(*clk->path.sym) > 1 && ns.size() != 1) clockOk = false;
        }
        if (!clockOk) {
            fallback("unresolved-clock", "could not identify a single clock event in this sequential block "
                     "(" + std::to_string(edgeEvents.size()) + " edge event(s))", pb.location);
        }
        std::vector<ResetInfo> resets;
        std::vector<const Event*> resetEvents;
        std::set<NodeId> resetNodes;
        std::vector<bool> unknownForced;   // per reset: the value it forces is not decided by this analysis (reported as '?')
        std::vector<bool> controlConditional;   // per reset: its forced value sits under a condition on another signal
        std::vector<std::set<NodeId>> nestedTargets;   // per reset: the register bits written under a test nested in its branch
        bool dualEdgeReset = false;
        for (auto* r : rsts) {
            auto ns = nodesFor(ctx, r->path);
            if (ns.empty()) continue;
            const std::string controlName = r->path.sym ? std::string(r->path.sym->name) : std::string("?");
            if (resetNodes.count(ns[0])) {
                // one control listed on both edges is one receiver (not two) whose polarity and forced value are undecided
                dualEdgeReset = true;
                for (size_t i = 0; i < resets.size(); i++) if (resets[i].signal == ns[0]) unknownForced[i] = true;
                fallback("reset-both-edges", "asynchronous control '" + controlName + "' is listed on both edges of one "
                         "sequential block; counted once with an unknown forced value", pb.location);
                continue;
            }
            ResetInfo ri;
            ri.signal = ns[0];
            ri.async = true;
            ri.activeLow = r->edge == 2;
            ri.fromEdge = true;
            resets.push_back(ri);
            resetEvents.push_back(r);
            resetNodes.insert(ns[0]);
            // a control the top-level if / case does not test is tested only inside the block: its forced value is not analysed
            const bool testedAtTop = topCond.count(canonPath(r->path)) > 0;
            unknownForced.push_back(topCase != nullptr || unanalysedControl || !testedAtTop);
            controlConditional.push_back(false);
            nestedTargets.emplace_back();
            if (topCase)
                fallback("reset-case", "asynchronous control '" + controlName + "' is tested by a case statement; the value it "
                         "forces is not analysed", pb.location);
            else if (unanalysedControl || !testedAtTop)
                fallback("reset-branch", "asynchronous control '" + controlName + "' is tested only inside the block (a nested if or "
                         "case); the value it forces is not analysed", pb.location);
        }
        // A copy of an asynchronous control (`wire r = rst_n`) read in this block is a read of the control, not a functional use
        // Only the bits the event names are controls: a sibling lane of a packed vector is still read as data (RDC-019, round 4 A3).
        for (const Event* ev : resetEvents) {
            const Path bits = canonPath(ev->path);
            const ValueSymbol* root = bits.sym;
            std::vector<const ValueSymbol*> family{root};
            std::unordered_set<const ValueSymbol*> familySeen{root};
            for (size_t fi = 0; fi < family.size(); fi++) {
                if (auto it = copiesOf.find(family[fi]); it != copiesOf.end())
                    for (const ValueSymbol* c : it->second) if (familySeen.insert(c).second) family.push_back(c);
            }
            for (const ValueSymbol* sym : family) {
                const bool sameShape = widthOf(*sym) == widthOf(*root);
                const Path named = sameShape ? Path{sym, bits.lo, bits.hi} : Path{sym, 0, widthOf(*sym) ? widthOf(*sym) - 1 : 0};
                for (NodeId n : nodesFor(ctx, named)) resetNodes.insert(n);
            }
        }
        // value each asynchronous control forces on each register: constants assigned in the branch that tests that control.
        // Evaluates the branch structurally: unrolls constant-bound `for` and fixed-size `foreach` loops, folds constant/parameter
        // `if` conditions, and joins runtime `if`/`case` arms so bits forced to the same constant on every path stay decided while
        // partial, disagreeing, or non-constant-loop writes stay undecided ('?') and conditional.
        using ResetBits = std::map<const ValueSymbol*, std::string>;
        std::vector<ResetBits> controlKinds(resets.size());   // per reset: symbol -> value per bit (LSB first)
        auto evalResetBranch = [&](const Statement& rootStmt, ResetBits& outState, std::set<NodeId>& condBits) {
            static constexpr size_t kResetUnrollBudget = 2048;
            size_t unrollWalks = 0, unrollFactor = 1;
            auto recordBit = [](std::string& bits, uint64_t b, char ch) {
                if (b >= bits.size()) return;
                if (bits[b] == '\0') bits[b] = ch;
                else if (bits[b] != ch) bits[b] = '?';
            };
            auto applyAssign = [&](const AssignmentExpression& a, ResetBits& state, bool forceUndecided, bool markCond) {
                if (!a.isCompound() && a.left().kind == ExpressionKind::Concatenation) {
                    std::vector<BitRef> targets, values;
                    const bool exact = !forceUndecided && bitList(a.left(), ec, targets) && bitList(a.right(), ec, values) &&
                                       targets.size() == values.size();
                    if (targets.empty()) {
                        for (const auto& p : pathsOf(a.left(), ec, true)) {
                            if (!p.sym || !isGraphSymbol(*p.sym)) continue;
                            uint64_t w = std::max<uint64_t>(widthOf(*p.sym), 1);
                            auto& bits = state.try_emplace(p.sym, std::string(w, '\0')).first->second;
                            for (uint64_t b = p.lo; b <= p.hi && b < w; b++) {
                                bits[b] = '?';
                                if (markCond) condBits.insert(nodeForBit(ctx, BitRef{p.sym, static_cast<uint32_t>(b), 0}));
                            }
                        }
                        return;
                    }
                    for (size_t i = 0; i < targets.size(); i++) {
                        const auto& t = targets[i];
                        if (!t.sym || !isGraphSymbol(*t.sym)) continue;
                        uint64_t w = std::max<uint64_t>(widthOf(*t.sym), 1);
                        auto& bits = state.try_emplace(t.sym, std::string(w, '\0')).first->second;
                        if (t.bit >= w) continue;
                        char ch = '?';
                        if (exact && values[i].sym == t.sym && values[i].bit == t.bit) ch = 'h';
                        else if (exact && !values[i].sym && (values[i].value == '0' || values[i].value == '1')) ch = values[i].value;
                        recordBit(bits, t.bit, ch);
                        if (markCond) condBits.insert(nodeForBit(ctx, BitRef{t.sym, t.bit, 0}));
                    }
                    return;
                }
                ValuePath vp(a.left(), ec);
                auto* vs = vp.rootSymbol();
                if (!vs || !isGraphSymbol(*vs)) return;
                uint64_t w = std::max<uint64_t>(widthOf(*vs), 1);
                auto& bits = state.try_emplace(vs, std::string(w, '\0')).first->second;   // '\0' = bit not assigned here
                uint64_t lo = 0, hi = w - 1;
                bool stat = false;
                if (vp.lsp) {
                    lo = vp.lspBounds.first;
                    hi = vp.lspBounds.second;
                    if (lo > hi) std::swap(lo, hi);
                    hi = std::min(hi, w - 1);
                    lo = std::min(lo, hi);
                    stat = vp.isFullyStatic();
                }
                if (forceUndecided) {
                    for (uint64_t b = lo; b <= hi; b++) {
                        bits[b] = '?';
                        if (markCond) condBits.insert(nodeForBit(ctx, BitRef{vs, static_cast<uint32_t>(b), 0}));
                    }
                    return;
                }
                bool selfHold = false;
                if (!a.isCompound() && (a.right().kind == ExpressionKind::NamedValue || a.right().kind == ExpressionKind::ElementSelect ||
                                        a.right().kind == ExpressionKind::RangeSelect || a.right().kind == ExpressionKind::HierarchicalValue)) {
                    ValuePath vr(a.right(), ec);
                    selfHold = vr.rootSymbol() == vs && (vp.lsp == nullptr) == (vr.lsp == nullptr) &&
                               (!vp.lsp || (vp.isFullyStatic() && vr.isFullyStatic() && vp.lspBounds == vr.lspBounds));
                }
                std::vector<BitRef> src;
                const bool exactSrc = !a.isCompound() && (!vp.lsp || vp.isFullyStatic()) &&
                                      (bitList(a.right(), ec, src) || maskedBits(a.right(), ec, src)) && src.size() == hi - lo + 1;
                auto cv = a.right().eval(ec);
                for (uint64_t b = lo; b <= hi; b++) {
                    char ch = '?';
                    if (exactSrc) {
                        const BitRef& s = src[hi - b];   // MSB first: entry hi - b is the bit at position b
                        if (s.sym == vs && s.bit == b) ch = 'h';
                        else if (!s.sym && s.value != 0) ch = s.value;   // a constant, X or Z (a masked X is '?')
                    } else if (selfHold) ch = 'h';
                    else if (stat && cv.isInteger() && b - lo < cv.integer().getBitWidth()) {
                        logic_t bit = cv.integer()[static_cast<int32_t>(b - lo)];
                        ch = bit.value == 0 ? '0' : bit.value == 1 ? '1' : bit.value == logic_t::z.value ? 'z' : 'x';
                    }
                    recordBit(bits, b, ch);
                }
            };
            auto markSubtreeUndecided = [&](const Statement& stmt, ResetBits& state, bool markCond) {
                struct Finder : ASTVisitor<Finder, VisitFlags::AllGood> {
                    decltype(applyAssign)& apply;
                    ResetBits& state;
                    bool markCond;
                    void handle(const AssignmentExpression& a) { apply(a, state, true, markCond); visitDefault(a); }
                } finder{{}, applyAssign, state, markCond};
                stmt.visit(finder);
            };
            auto joinArms = [&](const std::vector<ResetBits>& arms, ResetBits& dst) {
                if (arms.empty()) return;
                std::set<const ValueSymbol*> syms;
                for (const auto& arm : arms) for (const auto& [sym, _] : arm) syms.insert(sym);
                ResetBits joined;
                for (const ValueSymbol* sym : syms) {
                    const uint64_t w = std::max<uint64_t>(widthOf(*sym), 1);
                    std::string bits(w, '\0');
                    for (uint64_t b = 0; b < w; b++) {
                        auto at = [&](const ResetBits& m) -> char {
                            auto it = m.find(sym);
                            return (it != m.end() && b < it->second.size()) ? it->second[b] : '\0';
                        };
                        const char firstVal = at(arms[0]);
                        bool same = true;
                        for (size_t k = 1; k < arms.size(); k++) {
                            if (at(arms[k]) != firstVal) { same = false; break; }
                        }
                        if (same) {
                            bits[b] = firstVal;
                        } else {
                            bits[b] = '?';
                            condBits.insert(nodeForBit(ctx, BitRef{sym, static_cast<uint32_t>(b), 0}));
                        }
                    }
                    joined.emplace(sym, std::move(bits));
                }
                dst = std::move(joined);
            };
            std::function<void(const Statement&, ResetBits&)> walk = [&](const Statement& stmt, ResetBits& state) {
                switch (stmt.kind) {
                case StatementKind::Empty:
                case StatementKind::VariableDeclaration:
                    return;
                case StatementKind::Block: {
                    const auto& bs = stmt.as<BlockStatement>();
                    if (bs.blockKind != StatementBlockKind::Sequential) { markSubtreeUndecided(stmt, state, true); return; }
                    walk(bs.body, state);
                    return;
                }
                case StatementKind::List: {
                    bool afterJump = false;
                    for (const auto* child : stmt.as<StatementList>().list) {
                        if (afterJump) { markSubtreeUndecided(*child, state, true); continue; }
                        walk(*child, state);
                        JumpFinder jf;
                        child->visit(jf);
                        if (jf.found) afterJump = true;
                    }
                    return;
                }
                case StatementKind::ExpressionStatement: {
                    const auto& expr = stmt.as<ExpressionStatement>().expr;
                    if (expr.kind == ExpressionKind::Assignment)
                        applyAssign(expr.as<AssignmentExpression>(), state, false, false);
                    return;
                }
                case StatementKind::ForLoop: {
                    const auto& fl = stmt.as<ForLoopStatement>();
                    std::vector<LoopBinding> iterations;
                    if (unrollForLoop(fl, ec, iterations) && iterations.size() * unrollFactor + unrollWalks <= kResetUnrollBudget) {
                        unrollWalks += iterations.size() * unrollFactor;
                        const size_t savedFactor = unrollFactor;
                        unrollFactor *= std::max<size_t>(1, iterations.size());
                        const auto saved = boundLoops;
                        for (const auto& binding : iterations) {
                            for (size_t k = saved.size(); k < binding.size(); ++k) ec.createLocal(binding[k].first, binding[k].second);
                            boundLoops = binding;
                            walk(fl.body, state);
                            for (size_t k = saved.size(); k < binding.size(); ++k) ec.deleteLocal(binding[k].first);
                        }
                        boundLoops = saved;
                        unrollFactor = savedFactor;
                        return;
                    }
                    markSubtreeUndecided(fl.body, state, true);
                    return;
                }
                case StatementKind::ForeachLoop: {
                    const auto& fl = stmt.as<ForeachLoopStatement>();
                    std::vector<LoopBinding> iterations;
                    if (unrollForeachLoop(fl, ec, iterations) && iterations.size() * unrollFactor + unrollWalks <= kResetUnrollBudget) {
                        unrollWalks += iterations.size() * unrollFactor;
                        const size_t savedFactor = unrollFactor;
                        unrollFactor *= std::max<size_t>(1, iterations.size());
                        const auto saved = boundLoops;
                        for (const auto& binding : iterations) {
                            for (size_t k = saved.size(); k < binding.size(); ++k) ec.createLocal(binding[k].first, binding[k].second);
                            boundLoops = binding;
                            walk(fl.body, state);
                            for (size_t k = saved.size(); k < binding.size(); ++k) ec.deleteLocal(binding[k].first);
                        }
                        boundLoops = saved;
                        unrollFactor = savedFactor;
                        return;
                    }
                    markSubtreeUndecided(fl.body, state, true);
                    return;
                }
                case StatementKind::Conditional: {
                    const auto& cs = stmt.as<ConditionalStatement>();
                    if (cs.conditions.size() == 1 && !cs.conditions[0].pattern) {
                        if (design.hasCaseMode()) {
                            const int mv = modeValue(*cs.conditions[0].expr, ec);
                            if (mv >= 0) {
                                if (mv) walk(cs.ifTrue, state);
                                else if (cs.ifFalse) walk(*cs.ifFalse, state);
                                return;
                            }
                        }
                        if (auto cv = model::elaboratedCondition(*cs.conditions[0].expr, ec)) {
                            if (*cv) walk(cs.ifTrue, state);
                            else if (cs.ifFalse) walk(*cs.ifFalse, state);
                            return;
                        }
                    }
                    ResetBits tState = state, fState = state;
                    walk(cs.ifTrue, tState);
                    if (cs.ifFalse) walk(*cs.ifFalse, fState);
                    joinArms({tState, fState}, state);
                    return;
                }
                case StatementKind::Case: {
                    const auto& cs = stmt.as<CaseStatement>();
                    std::vector<ResetBits> arms;
                    arms.reserve(cs.items.size() + 1);
                    for (const auto& item : cs.items) {
                        ResetBits arm = state;
                        walk(*item.stmt, arm);
                        arms.push_back(std::move(arm));
                    }
                    if (cs.defaultCase) {
                        ResetBits arm = state;
                        walk(*cs.defaultCase, arm);
                        arms.push_back(std::move(arm));
                    } else {
                        arms.push_back(state);
                    }
                    joinArms(arms, state);
                    return;
                }
                default:
                    markSubtreeUndecided(stmt, state, true);
                    return;
                }
            };
            walk(rootStmt, outState);
        };
        // 3-valued evaluation of a reset guard under the assumption that async control `ri` is asserted
        // (1 = guaranteed true, 0 = guaranteed false, -1 = unknown). Only exact single-driver copies of the
        // control match it; multiply-driven nets (`multiDrivenSyms`) stay unknown.
        auto evalUnderReset = [&](const BoolFactPtr& fact, size_t ri) {
            const Path target = canonPath(resetEvents[ri]->path);
            const int assertedVal = resets[ri].activeLow ? 0 : 1;
            std::function<int(const BoolFactPtr&)> eval = [&](const BoolFactPtr& f) -> int {
                if (!f) return -1;
                switch (f->kind) {
                case BoolFact::Kind::Constant: return f->value ? 1 : 0;
                case BoolFact::Kind::Node: {
                    if (f->node == kNoNode || f->node >= m.nodes.size()) return -1;
                    const Node& nd = m.nodes[f->node];
                    if (!nd.sym || multiDrivenSyms.count(nd.sym)) return -1;
                    return canonPath(Path{nd.sym, nd.bit, nd.bit}) == target ? assertedVal : -1;
                }
                case BoolFact::Kind::Not: {
                    if (f->args.size() != 1) return -1;
                    const int v = eval(f->args[0]);
                    return v < 0 ? -1 : (1 - v);
                }
                case BoolFact::Kind::Or: {
                    bool anyUnknown = false;
                    for (const auto& a : f->args) {
                        const int v = eval(a);
                        if (v == 1) return 1;
                        if (v < 0) anyUnknown = true;
                    }
                    return anyUnknown ? -1 : 0;
                }
                case BoolFact::Kind::And: {
                    bool anyUnknown = false;
                    for (const auto& a : f->args) {
                        const int v = eval(a);
                        if (v == 0) return 0;
                        if (v < 0) anyUnknown = true;
                    }
                    return anyUnknown ? -1 : 1;
                }
                case BoolFact::Kind::Equal: {
                    if (f->args.size() != 2) return -1;
                    const int a = eval(f->args[0]), b = eval(f->args[1]);
                    return (a >= 0 && b >= 0) ? (a == b ? 1 : 0) : -1;
                }
                case BoolFact::Kind::Unknown: return -1;
                }
                return -1;
            };
            return eval(fact);
        };
        struct ExtraGuardBranch {
            std::vector<std::pair<NodeId, bool>> guards;
            ResetBits bits;
            std::set<NodeId> nested;
        };
        std::vector<ExtraGuardBranch> extraGuardBranches;
        std::function<void(const BoolFactPtr&, std::vector<std::pair<NodeId, bool>>&)> collectOrLiterals =
            [&](const BoolFactPtr& f, std::vector<std::pair<NodeId, bool>>& out) {
                if (!f) return;
                if (f->kind == BoolFact::Kind::Or) {
                    for (const auto& arg : f->args) collectOrLiterals(arg, out);
                    return;
                }
                if (f->kind == BoolFact::Kind::Node && f->node != kNoNode) {
                    out.emplace_back(f->node, false);
                    return;
                }
                if (f->kind == BoolFact::Kind::Not && f->args.size() == 1 &&
                    f->args[0] && f->args[0]->kind == BoolFact::Kind::Node && f->args[0]->node != kNoNode) {
                    out.emplace_back(f->args[0]->node, true);
                }
            };
        bool priorNonResetCond = false;
        for (const Statement* cur = topIf; cur && cur->kind == StatementKind::Conditional;) {
            auto& c = cur->as<ConditionalStatement>();
            std::set<Path> condPaths;
            bool condMultiDriven = false;
            for (auto& cc : c.conditions) expandCondPaths(*cc.expr, condPaths, &condMultiDriven);
            bool curHasNonOwnControl = condMultiDriven;
            for (const auto& p : condPaths) {
                bool ownControl = false;
                for (const auto* event : resetEvents) ownControl = ownControl || p == canonPath(event->path);
                if (!ownControl) curHasNonOwnControl = true;
            }
            std::vector<std::pair<NodeId, bool>> orLiterals;
            if (!priorNonResetCond && c.conditions.size() == 1 && !c.conditions[0].pattern) {
                collectOrLiterals(booleanOf(*c.conditions[0].expr, ec, ctx), orLiterals);
            }
            bool matchedAnyReset = false;
            bool recordedOrForThisCond = false;
            for (size_t ri = 0; ri < resetEvents.size(); ri++) {
                if (!condPaths.count(canonPath(resetEvents[ri]->path))) continue;
                matchedAnyReset = true;
                // Another condition on the test, a multiply-driven guard, or a test nested in the branch decides whether the control forces its value.
                // An OR-combined guard (`!rst || other`, `rst || other`) is still decided when asserting this reset forces the guard true.
                {
                    if (priorNonResetCond) {
                        controlConditional[ri] = true;
                    } else if (curHasNonOwnControl) {
                        const bool implied = c.conditions.size() == 1 && !c.conditions[0].pattern &&
                                             evalUnderReset(booleanOf(*c.conditions[0].expr, ec, ctx), ri) == 1;
                        if (!implied) controlConditional[ri] = true;
                    }
                }
                // `ifTrue` runs while the control is asserted only when the test is the control at its asserted level; the
                // else arm of `if (rst_n) ...` is the reset branch. Such a branch is not read as a forced value here.
                if (c.conditions.size() == 1 && !c.conditions[0].pattern) {
                    auto fact = booleanOf(*c.conditions[0].expr, ec, ctx);
                    bool low = false;
                    if (fact && fact->kind == BoolFact::Kind::Not && fact->args.size() == 1) {
                        low = true;
                        fact = fact->args[0];
                    }
                    if (fact && fact->kind == BoolFact::Kind::Node && fact->node == resets[ri].signal && low != resets[ri].activeLow)
                        unknownForced[ri] = true;
                }
                if (unknownForced[ri]) continue;
                evalResetBranch(c.ifTrue, controlKinds[ri], nestedTargets[ri]);
                if (!priorNonResetCond && !orLiterals.empty() && !recordedOrForThisCond) {
                    ExtraGuardBranch br;
                    for (const auto& [g, low] : orLiterals)
                        if (!resetNodes.count(g)) br.guards.emplace_back(g, low);
                    if (!br.guards.empty()) {
                        br.bits = controlKinds[ri];
                        br.nested = nestedTargets[ri];
                        extraGuardBranches.push_back(std::move(br));
                        recordedOrForThisCond = true;
                    }
                }
            }
            if (!resets.empty() && !matchedAnyReset && !priorNonResetCond && !orLiterals.empty()) {
                ExtraGuardBranch br;
                for (const auto& [g, low] : orLiterals)
                    if (!resetNodes.count(g)) br.guards.emplace_back(g, low);
                if (!br.guards.empty()) {
                    evalResetBranch(c.ifTrue, br.bits, br.nested);
                    extraGuardBranches.push_back(std::move(br));
                }
            }
            priorNonResetCond = priorNonResetCond || curHasNonOwnControl;
            cur = c.ifFalse ? firstMeaningful(c.ifFalse) : nullptr;
        }
        // Guard formulas are deliberately small: a conjunction of known-true
        // constants and 1-bit signal literals. Negation applies to the next
        // literal; negated conjunctions (disjunctions) remain unknown.
        const auto collectResetGuards = [&](const Expression& expression, std::map<NodeId, bool>& guards) {
            std::function<bool(const Expression&, bool)> collect = [&](const Expression& expr, bool negated) {
                const Expression* term = &expr;
                while (term->kind == ExpressionKind::Conversion) term = &term->as<ConversionExpression>().operand();
                if (term->kind == ExpressionKind::UnaryOp) {
                    const auto& unary = term->as<UnaryExpression>();
                    if (unary.op != UnaryOperator::LogicalNot && unary.op != UnaryOperator::BitwiseNot) return false;
                    return collect(unary.operand(), !negated);
                }
                if (term->kind == ExpressionKind::BinaryOp && term->as<BinaryExpression>().op == BinaryOperator::LogicalAnd) {
                    if (negated) return false;
                    const auto& binary = term->as<BinaryExpression>();
                    return collect(binary.left(), false) && collect(binary.right(), false);
                }
                auto cv = term->eval(ec);
                if (cv.isInteger() && !cv.hasUnknown()) return cv.isTrue() != negated;
                if (term->kind != ExpressionKind::NamedValue) return false;
                auto paths = pathsOf(*term, ec, false);
                if (paths.size() != 1 || pathWidth(paths[0]) != 1) return false;
                auto nodes = nodesFor(ctx, paths[0]);
                if (nodes.size() != 1) return false;
                auto [it, inserted] = guards.emplace(nodes[0], negated);
                return inserted || it->second == negated;
            };
            return collect(expression, false) && !guards.empty();
        };

        // heuristic synchronous reset candidate: top-level supported guard, then-branch assigns constants
        bool syncCand = false, syncLow = false;
        std::map<NodeId, bool> syncGuardPolarity;
        std::map<NodeId, char> syncResetValues;
        if (resets.empty() && topIf && topIf->conditions.size() == 1) {
            const Expression& test = *topIf->conditions[0].expr;
            auto whole = test.eval(ec);
            const bool constantCondition = whole.isInteger() && !whole.hasUnknown();
            if (!constantCondition && collectResetGuards(test, syncGuardPolarity)) {
                std::vector<Assign> tmp;
                StmtVisitor tv(*this, ec, tmp, ctx);
                topIf->ifTrue.visit(tv);
                bool allConst = !tmp.empty();
                // Constants contribute no data reads. Reading the condition itself (`if (s) t <= s`) is data, not a reset value.
                for (auto& a : tmp) if (!a.dataReads.empty()) allConst = false;
                syncCand = allConst;
                if (syncCand) {
                    for (const auto& a : tmp) if (a.pairsValid) for (const auto& [target, source] : a.pairs) {
                        if (!target.sym || source.sym) continue;
                        const NodeId bit = nodeForBit(ctx, target);
                        auto [it, inserted] = syncResetValues.emplace(bit, source.value);
                        if (!inserted && it->second != source.value) it->second = '?';
                    }
                    syncLow = syncGuardPolarity.begin()->second;
                }
            }
        }

        // A trailing suffix of else-free constant overrides can be equivalent to a
        // reset-first branch. `trailingResetGuards` (used for CDC synchroniser guard stripping)
        // requires one unconditional exact/inverted NBA earlier; `trailingSyncResetCandidates`
        // records any trailing constant-override reset for RDC-001/RDC-010/RDC-020/RDC-025.
        // Keep the control edges: a reset driven from another domain is still a crossing.
        std::map<NodeId, std::map<std::pair<NodeId, bool>, char>> trailingResetGuards;
        std::map<NodeId, std::map<std::pair<NodeId, bool>, char>> trailingSyncResetCandidates;
        std::set<NodeId> unconditionalOverrideBits;
        bool unqualifiedClock = std::all_of(events.begin(), events.end(), [](const Event& e) { return !e.hasIff && e.iff.empty(); });
        const bool anyAfterExit = std::any_of(assigns.begin(), assigns.end(), [](const Assign& a) { return a.afterPossibleExit; });
        if (resets.empty() && unqualifiedClock && !anyAfterExit && first && first->kind == StatementKind::Timed) {
            auto statements = [](const Statement* stmt) {
                std::vector<const Statement*> result;
                while (stmt && stmt->kind == StatementKind::Block) stmt = &stmt->as<BlockStatement>().body;
                if (stmt && stmt->kind == StatementKind::List) {
                    for (auto* item : stmt->as<StatementList>().list)
                        if (item->kind != StatementKind::VariableDeclaration) result.push_back(item);
                } else if (stmt) result.push_back(stmt);
                return result;
            };
            auto list = statements(&first->as<TimedStatement>().stmt);
            if (list.size() >= 2 && list.back()->kind == StatementKind::Conditional) {
                // Walk backward across a trailing suffix of else-free constant-override conditionals.
                // Stop cleanly when an earlier functional conditional is reached, keeping the valid
                // trailing reset overrides already collected.
                size_t suffix = list.size();
                std::map<NodeId, char> forced;
                std::map<NodeId, std::map<NodeId, bool>> forcingGuards;
                std::set<NodeId> conflictedBits;
                while (suffix > 0 && list[suffix - 1]->kind == StatementKind::Conditional) {
                    const auto& c = list[suffix - 1]->as<ConditionalStatement>();
                    if (c.ifFalse || c.conditions.size() != 1 || c.conditions[0].pattern) break;
                    const Expression* test = c.conditions[0].expr;
                    auto whole = test->eval(ec);
                    if (whole.isInteger() && !whole.hasUnknown()) {
                        if (!whole.isTrue()) { --suffix; continue; } // provably dead write
                        auto branch = statements(&c.ifTrue);
                        std::vector<std::vector<BitRef>> writes;
                        bool allUnconditionalConstants = !branch.empty();
                        for (const auto* stmt : branch) {
                            if (stmt->kind != StatementKind::ExpressionStatement ||
                                stmt->as<ExpressionStatement>().expr.kind != ExpressionKind::Assignment) {
                                allUnconditionalConstants = false; break;
                            }
                            const auto& assignment = stmt->as<ExpressionStatement>().expr.as<AssignmentExpression>();
                            auto value = assignment.right().eval(ec);
                            std::vector<BitRef> targets;
                            if (!assignment.isNonBlocking() || assignment.isCompound() || !value.isInteger() ||
                                !bitList(assignment.left(), ec, targets) || targets.empty() ||
                                std::any_of(targets.begin(), targets.end(), [](const BitRef& target) { return target.sym == nullptr; })) {
                                allUnconditionalConstants = false; break;
                            }
                            writes.push_back(std::move(targets));
                        }
                        if (allUnconditionalConstants)
                            for (const auto& targets : writes) for (const auto& target : targets)
                                unconditionalOverrideBits.insert(nodeForBit(ctx, target));
                        break; // unconditional override is not a reset candidate; stop extending suffix
                    }
                    std::map<NodeId, bool> conditionGuards;
                    if (!collectResetGuards(*test, conditionGuards)) break;
                    auto branch = statements(&c.ifTrue);
                    if (branch.empty()) break;
                    std::vector<std::pair<NodeId, char>> stepWrites;
                    bool stepOk = true;
                    for (const auto* stmt : branch) {
                        if (stmt->kind != StatementKind::ExpressionStatement) { stepOk = false; break; }
                        const auto& expr = stmt->as<ExpressionStatement>().expr;
                        if (expr.kind != ExpressionKind::Assignment) { stepOk = false; break; }
                        const auto& assignment = expr.as<AssignmentExpression>();
                        std::vector<Assign> parsed;
                        StmtVisitor visitor(*this, ec, parsed, ctx);
                        stmt->visit(visitor);
                        std::vector<BitRef> targets;
                        auto value = assignment.right().eval(ec);
                        if (!assignment.isNonBlocking() || assignment.isCompound() || !value.isInteger() || value.integer().hasUnknown() ||
                            parsed.size() != 1 || !parsed[0].ctrlReads.empty() || !parsed[0].dataReads.empty() ||
                            !parsed[0].uncertainCalls.empty() || !bitList(assignment.left(), ec, targets) || targets.empty()) {
                            stepOk = false; break;
                        }
                        const auto bits = value.integer();
                        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
                            const auto& target = targets[targetIndex];
                            if (!target.sym) { stepOk = false; break; }
                            const NodeId bit = nodeForBit(ctx, target);
                            const auto valueIndex = bits.getBitWidth() - 1 - targetIndex;
                            const char force = bits[static_cast<int32_t>(valueIndex)] ? '1' : '0';
                            stepWrites.emplace_back(bit, force);
                        }
                        if (!stepOk) break;
                    }
                    if (!stepOk) break;
                    for (const auto& [bit, force] : stepWrites) {
                        auto [it, inserted] = forced.emplace(bit, force);
                        if (!inserted && it->second != force) conflictedBits.insert(bit);
                        for (const auto& [guard, low] : conditionGuards) {
                            auto [guardIt, guardInserted] = forcingGuards[bit].emplace(guard, low);
                            if (!guardInserted && guardIt->second != low) conflictedBits.insert(bit);
                        }
                    }
                    --suffix;
                }
                if (suffix < list.size() && !forcingGuards.empty()) {
                    std::map<NodeId, unsigned> priorWrites;
                    std::set<NodeId> exactData;
                    std::map<NodeId, NodeId> dataSource;
                    std::set<NodeId> blockingWrittenBits;
                    for (size_t i = 0; i < suffix; ++i) {
                        std::vector<Assign> prior;
                        StmtVisitor visitor(*this, ec, prior, ctx);
                        list[i]->visit(visitor);
                        for (const auto& a : prior) for (const auto& target : a.targets)
                            for (auto bit : nodesFor(ctx, target)) {
                                ++priorWrites[bit];
                                if (!a.nonBlocking) blockingWrittenBits.insert(bit);
                            }
                        if (list[i]->kind != StatementKind::ExpressionStatement || prior.size() != 1) continue;
                        const auto& a = prior[0];
                        if (!a.nonBlocking || !(a.pairsValid || a.flipPairsValid) || !a.ctrlReads.empty() || a.afterPossibleExit) continue;
                        for (const auto& [target, source] : a.pairs)
                            if (target.sym && source.sym) {
                                const NodeId targetBit = nodeForBit(ctx, target);
                                exactData.insert(targetBit);
                                dataSource[targetBit] = nodeForBit(ctx, source);
                            }
                    }
                    for (const auto& [bit, force] : forced) {
                        if (conflictedBits.count(bit) || unconditionalOverrideBits.count(bit)) continue;
                        const auto& guards = forcingGuards[bit];
                        if (guards.empty()) continue;
                        bool aliasesData = false;
                        bool blockingGuard = false;
                        for (const auto& [guard, low] : guards) {
                            if (dataSource.count(bit) && dataSource[bit] == guard) aliasesData = true;
                            if (blockingWrittenBits.count(guard)) blockingGuard = true;
                        }
                        if (blockingGuard) continue;
                        if (priorWrites[bit] == 1 && exactData.count(bit) && !aliasesData)
                            for (const auto& [guard, low] : guards)
                                trailingResetGuards[bit][{guard, low}] = force;
                        else if (!aliasesData)
                            for (const auto& [guard, low] : guards)
                                trailingSyncResetCandidates[bit][{guard, low}] = force;
                    }
                }
                // Keep the established single-final-override path for older
                // supported forms whose surrounding block syntax is not a
                // suffix of simple conditionals (for example, unrelated writes
                // in the same process).
                if (trailingResetGuards.empty() && trailingSyncResetCandidates.empty()) {
                    const auto& last = list.back()->as<ConditionalStatement>();
                    if (!last.ifFalse && last.conditions.size() == 1 && !last.conditions[0].pattern) {
                        const Expression* cond = last.conditions[0].expr;
                        while (cond->kind == ExpressionKind::Conversion) cond = &cond->as<ConversionExpression>().operand();
                        bool low = false;
                        if (cond->kind == ExpressionKind::UnaryOp) {
                            const auto& unary = cond->as<UnaryExpression>();
                            if (unary.op == UnaryOperator::LogicalNot || unary.op == UnaryOperator::BitwiseNot) {
                                low = true; cond = &unary.operand();
                                while (cond->kind == ExpressionKind::Conversion) cond = &cond->as<ConversionExpression>().operand();
                            }
                        }
                        auto paths = pathsOf(*cond, ec, false);
                        auto guards = paths.size() == 1 && pathWidth(paths[0]) == 1 ? nodesFor(ctx, paths[0]) : std::vector<NodeId>{};
                        auto branch = statements(&last.ifTrue);
                        std::set<NodeId> overrides, exactData;
                        std::map<NodeId, char> overrideValues;
                        std::map<NodeId, NodeId> dataSource;
                        std::map<NodeId, unsigned> priorWrites;
                        bool valid = cond->kind == ExpressionKind::NamedValue && guards.size() == 1 && !branch.empty();
                        for (const auto* stmt : branch) {
                            if (stmt->kind != StatementKind::ExpressionStatement || stmt->as<ExpressionStatement>().expr.kind != ExpressionKind::Assignment) { valid = false; break; }
                            const auto& assignment = stmt->as<ExpressionStatement>().expr.as<AssignmentExpression>();
                            std::vector<Assign> parsed;
                            StmtVisitor visitor(*this, ec, parsed, ctx);
                            stmt->visit(visitor);
                            std::vector<BitRef> targets;
                            auto value = assignment.right().eval(ec);
                            if (!assignment.isNonBlocking() || assignment.isCompound() || !value.isInteger() || value.integer().hasUnknown() ||
                                parsed.size() != 1 || !parsed[0].ctrlReads.empty() || !bitList(assignment.left(), ec, targets) || targets.empty()) { valid = false; break; }
                            for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
                                const auto& target = targets[targetIndex];
                                if (target.sym) {
                                const NodeId bit = nodeForBit(ctx, target);
                                overrides.insert(bit);
                                auto bits = value.integer();
                                const auto valueIndex = bits.getBitWidth() - 1 - targetIndex;
                                overrideValues[bit] = bits[static_cast<int32_t>(valueIndex)] ? '1' : '0';
                                } else valid = false;
                            }
                        }
                        for (size_t i = 0; i + 1 < list.size(); ++i) {
                            std::vector<Assign> prior;
                            StmtVisitor visitor(*this, ec, prior, ctx);
                            list[i]->visit(visitor);
                            for (const auto& a : prior) for (const auto& target : a.targets)
                                for (auto bit : nodesFor(ctx, target)) ++priorWrites[bit];
                            if (list[i]->kind != StatementKind::ExpressionStatement || prior.size() != 1) continue;
                            const auto& a = prior[0];
                            if (!a.nonBlocking || !(a.pairsValid || a.flipPairsValid) || !a.ctrlReads.empty() || a.afterPossibleExit) continue;
                            for (const auto& [target, source] : a.pairs) if (target.sym && source.sym) {
                                const NodeId bit = nodeForBit(ctx, target);
                                exactData.insert(bit); dataSource[bit] = nodeForBit(ctx, source);
                            }
                        }
                        if (valid) for (auto bit : overrides)
                            if (priorWrites[bit] == 1 && exactData.count(bit) && dataSource[bit] != guards[0])
                                trailingResetGuards[bit][{guards[0], low}] = overrideValues[bit];
                    }
                }
            }
        }

        // --- `@(posedge clk iff cond)`: the qualifier is an enable, i.e. a control dependency of every target
        for (auto& e : events)
            for (auto& a : assigns)
                for (auto& p : e.iff) {
                    a.ctrlReads.push_back(p);
                    GuardCondition qualifier;
                    std::vector<BoolFactPtr> refs;
                    for (auto node : nodesFor(ctx, p)) {
                        qualifier.dependencies.push_back(node);
                        refs.push_back(boolFact(BoolFact::Kind::Node, {}, node));
                    }
                    qualifier.predicate = boolFact(BoolFact::Kind::Unknown, std::move(refs));
                    a.booleanGuards.push_back(std::move(qualifier));
                }

        for (const auto& event : events) for (auto& assignment : assigns)
            assignment.uncertainCalls.insert(assignment.uncertainCalls.end(), event.uncertainCalls.begin(), event.uncertainCalls.end());

        // Establish exact asynchronous transitions separately from the reset-value heuristic.
        // Only straight-line assignment branches are certified: no enables, loops, timing or calls.
        struct Transition { char asserted = '?'; BitRef released{}; };
        std::map<NodeId, Transition> transitions;
        auto straight = [&](auto&& self, const Statement& stmt) -> bool {
            if (stmt.kind == StatementKind::Block) return self(self, stmt.as<BlockStatement>().body);
            if (stmt.kind == StatementKind::List) {
                for (auto* child : stmt.as<StatementList>().list) if (!self(self, *child)) return false;
                return true;
            }
            return stmt.kind == StatementKind::ExpressionStatement &&
                   stmt.as<ExpressionStatement>().expr.kind == ExpressionKind::Assignment;
        };
        // firstMeaningful intentionally selects the first statement; do not certify hidden siblings.
        auto only = [&](auto&& self, const Statement* stmt) -> const Statement* {
            if (!stmt) return nullptr;
            if (stmt->kind == StatementKind::Block) return self(self, &stmt->as<BlockStatement>().body);
            if (stmt->kind == StatementKind::List) {
                const Statement* found = nullptr;
                for (auto* child : stmt->as<StatementList>().list) {
                    if (child->kind == StatementKind::VariableDeclaration) continue;
                    if (found) return nullptr;
                    found = child;
                }
                return self(self, found);
            }
            return stmt;
        };
        const Statement* sole = only(only, &pb.getBody());
        if (sole && sole->kind == StatementKind::Timed) sole = only(only, &sole->as<TimedStatement>().stmt);
        bool plainEvents = true;
        bool anyEventHasIff = false;
        for (const auto& e : events) {
            if (e.hasIff || !e.iff.empty()) anyEventHasIff = true;
            if (e.hasIff || !e.iff.empty() || !e.uncertainCalls.empty()) plainEvents = false;
        }
        if (plainEvents && !dualEdgeReset && sole == topIf && resets.size() == 1 && topIf && topIf->ifFalse &&
            topIf->conditions.size() == 1 && !topIf->conditions[0].pattern &&
            straight(straight, topIf->ifTrue) && straight(straight, *topIf->ifFalse)) {
            auto cond = booleanOf(*topIf->conditions[0].expr, ec, ctx);
            bool low = false;
            if (cond && cond->kind == BoolFact::Kind::Not && cond->args.size() == 1) {
                low = true;
                cond = cond->args[0];
            }
            if (cond && cond->kind == BoolFact::Kind::Node && cond->node == resets[0].signal) {
                bool assertionTrue = low == resets[0].activeLow;
                std::vector<Assign> asserted, released;
                StmtVisitor av(*this, ec, asserted, ctx), rv(*this, ec, released, ctx);
                (assertionTrue ? topIf->ifTrue : *topIf->ifFalse).visit(av);
                (assertionTrue ? *topIf->ifFalse : topIf->ifTrue).visit(rv);
                auto bits = [&](const std::vector<Assign>& writes) {
                    std::map<NodeId, BitRef> result;
                    std::set<NodeId> bad;
                    for (auto& a : writes) {
                        bool exact = a.nonBlocking && a.pairsValid && a.ctrlReads.empty() && a.uncertainCalls.empty();
                        for (auto& t : a.targets) for (auto n : nodesFor(ctx, t)) if (!exact) bad.insert(n);
                        if (!exact) continue;
                        for (auto& [t, src] : a.pairs) {
                            auto n = nodeForBit(ctx, t);
                            if (!result.emplace(n, src).second) bad.insert(n);
                        }
                    }
                    for (auto n : bad) result.erase(n);
                    return result;
                };
                auto aa = bits(asserted), rr = bits(released);
                for (auto& [n, src] : aa) {
                    auto it = rr.find(n);
                    if (!src.sym && (src.value == '0' || src.value == '1') && it != rr.end())
                        transitions[n] = {src.value, it->second};
                }
            }
        }

        // --- edges and flops
        // Filter out dead writes that Flow proves are completely overwritten
        // in the same procedure before any read, observation, or exposure.
        std::unique_ptr<flow::Flow> procFlow;
        bool exactProcFlow = false;
        if (pb.procedureKind != ProceduralBlockKind::AlwaysLatch) {
            std::unordered_set<const ValueSymbol*> writtenOnce;
            bool hasMultiWrite = false;
            for (const auto& a : assigns) {
                if (!a.asg) continue;
                for (const auto& t : a.targets) {
                    if (!writtenOnce.insert(t.sym).second) { hasMultiWrite = true; break; }
                }
                if (hasMultiWrite) break;
            }
            if (hasMultiWrite) {
                procFlow = std::make_unique<flow::Flow>(pb);
                exactProcFlow = !procFlow->bad && !procFlow->approximated && !procFlow->opaque &&
                                !procFlow->timing && !procFlow->walkedOnce;
            }
        }
        auto isDeadWrite = [&](const Assign& a, const Path& t) -> bool {
            if (!exactProcFlow || !a.asg || !t.sym) return false;
            bool sawMatch = false;
            for (const auto& acc : procFlow->accesses) {
                if (!acc.write || acc.asg != a.asg || acc.sym != t.sym) continue;
                if (acc.range.second < t.lo || acc.range.first > t.hi) continue;
                sawMatch = true;
                if (!acc.exact || acc.live || acc.used || acc.observed || acc.exposed) return false;
            }
            return sawMatch;
        };
        std::set<NodeId> qs;
        // Decide once per symbol: later conditional writes can make an earlier
        // blocking assignment stateful. Mixing Comb and Seq for the same symbol
        // would hide the first assignment from the register's fan-in cone.
        std::set<const ValueSymbol*> blockingState;
        std::set<const ValueSymbol*> readSoFar;
        auto isOutputPort = [](const ValueSymbol& sym) {
            for (const auto* backref = sym.getFirstPortBackref(); backref; backref = backref->getNextBackreference()) {
                if (backref->port && (backref->port->direction == ArgumentDirection::Out ||
                                      backref->port->direction == ArgumentDirection::InOut))
                    return true;
            }
            return false;
        };
        for (const auto& a : assigns) {
            for (auto* v : {&a.dataReads, &a.ctrlReads})
                for (auto& r : *v) readSoFar.insert(r.sym);
            if (a.nonBlocking) continue;
            for (auto& t : a.targets)
                if (isPackageState(*t.sym) || (isModuleLevel(*t.sym) &&
                    (!a.uncertainCalls.empty() || !a.ctrlReads.empty() || readSoFar.count(t.sym) ||
                     readByOther(t.sym, &pb) || isOutputPort(*t.sym))))
                    blockingState.insert(t.sym);
        }
        for (auto* sym : blockingState)
            fallback("blocking-assignment-register",
                     "'" + std::string(sym->name) + "' is assigned with a blocking assignment in a clocked "
                     "block and keeps state; treated as a register but never as a synchroniser stage", pb.location);
        std::unordered_set<Model::DeadEdgeKey, Model::DeadEdgeHash> procLiveEdges;
        curLiveProcEdges = exactProcFlow ? &procLiveEdges : nullptr;
        struct LiveEdgesGuard { Builder& b; ~LiveEdgesGuard() { b.curLiveProcEdges = nullptr; b.curDeadWrite = false; } } liveGuard{*this};
        struct DeferredDead {
            const Assign* a = nullptr;
            std::vector<Path> localTargets, moduleTargets;
        };
        std::vector<DeferredDead> deferredDead;
        auto emitModuleExtras = [&](const Assign& a, const std::vector<Path>& moduleTargets) {
            std::vector<Path> asyncReads;
            for (auto* v : {&a.ctrlReads, &a.dataReads})
                for (auto& r : *v)
                    for (auto n : nodesFor(ctx, r))
                        if (resetNodes.count(n)) { asyncReads.push_back(r); break; }
            if (!asyncReads.empty()) connect(ctx, asyncReads, ctx, moduleTargets, EdgeType::AsyncCtl, proc, pb.location);
            for (auto& t : moduleTargets)
                for (auto n : nodesFor(ctx, t)) qs.insert(n);
        };
        curDeadWrite = false;
        auto rejectPaths = [&](const std::vector<Path>& ps) {
            for (const auto& t : ps)
                for (NodeId n : nodesFor(ctx, t)) m.stuckRejected.insert(n);
        };
        bool procHasUnattributedWrite = false;
        for (const auto& a : assigns) {
            if (a.unattributedWrite) { procHasUnattributedWrite = true; break; }
        }
        // Intra-assignment delays (`q <= #1 d`) make every write of this block a delayed copy, as does a delay statement in its body.
        const bool procDelayed = procHasBodyTiming || std::any_of(assigns.begin(), assigns.end(),
            [](const Assign& a) { return a.asg && a.asg->timingControl != nullptr; });
        if (procDelayed) m.procs[proc].delayed = true;
        if (procHasStmtTiming) m.procs[proc].bodyTiming = true;
        // Block-local counters written solely inside an async-reset branch without reading any non-self signal as data
        // (e.g. `int i; i = 0; while (i < 4) ... i++;` or `repeat (4)` counters) are loop indices, not functional uses of the reset.
        std::unordered_set<const ValueSymbol*> resetLocalCounters;
        if (!resetNodes.empty()) {
            std::unordered_map<const ValueSymbol*, bool> cand;
            auto hasResetCtrl = [&](const Assign& a) {
                for (const auto& r : a.ctrlReads)
                    for (NodeId n : nodesFor(ctx, r))
                        if (resetNodes.count(n)) return true;
                return false;
            };
            for (const auto& a : assigns) {
                for (const auto& t : a.targets) {
                    if (!t.sym || isModuleLevel(*t.sym) || isPackageState(*t.sym)) continue;
                    auto [it, fresh] = cand.emplace(t.sym, true);
                    if (!it->second) continue;
                    if (!hasResetCtrl(a)) { it->second = false; continue; }
                    for (const auto& r : a.dataReads) {
                        if (r.sym != t.sym) { it->second = false; break; }
                    }
                }
            }
            for (const auto& [sym, ok] : cand) if (ok) resetLocalCounters.insert(sym);
        }
        for (auto& a : assigns) {
            std::vector<Path> liveLocal, liveModule, deadLocal, deadModule;
            for (auto& t : a.targets) {
                const bool state = (isModuleLevel(*t.sym) || isPackageState(*t.sym)) &&
                    (a.nonBlocking || blockingState.count(t.sym));
                if (isDeadWrite(a, t)) (state ? deadModule : deadLocal).push_back(t);
                else (state ? liveModule : liveLocal).push_back(t);
            }
            if (!liveLocal.empty()) {
                // Loop-index declarations/counters inside the async reset branch (e.g. `for (int i = 0; ...)`,
                // `int i; i = 0; while (...) ... i++;`) are guarded by the async reset condition, not using the
                // reset as functional data; skip `resetNodes` only on loop-variable/counter targets (other locals
                // keep their normal control edges for qualification checks).
                std::vector<Path> emitLoopLocal, emitOtherLocal;
                for (const auto& t : liveLocal) {
                    if (m.loopVariables.count(t.sym) || resetLocalCounters.count(t.sym)) {
                        emitLoopLocal.push_back(t);
                    } else {
                        emitOtherLocal.push_back(t);
                    }
                }
                if (!emitLoopLocal.empty())
                    emitAssign(ctx, a, emitLoopLocal, EdgeType::Comb, proc, pb.location, nullptr, &resetNodes);
                if (!emitOtherLocal.empty())
                    emitAssign(ctx, a, emitOtherLocal, EdgeType::Comb, proc, pb.location);
            }
            if (!liveModule.empty()) {
                emitAssign(ctx, a, liveModule, EdgeType::Seq, proc, pb.location, &resetNodes);
                emitModuleExtras(a, liveModule);
                const bool uncondWrite = !a.afterPossibleExit && a.ctrlReads.empty() && a.booleanGuards.empty();
                if (procHasUnattributedWrite || procHasBodyTiming || (a.asg && a.asg->timingControl != nullptr) ||
                    a.called || a.approximate || a.unattributedWrite || !a.uncertainCalls.empty() || !a.nonBlocking) {
                    rejectPaths(liveModule);
                } else if (a.pairsValid) {
                    for (const auto& [t, src] : a.pairs) {
                        if (!t.sym) continue;
                        const bool live = std::any_of(liveModule.begin(), liveModule.end(), [&](const Path& p) {
                            return p.sym == t.sym && t.bit >= p.lo && t.bit <= p.hi;
                        });
                        if (!live) continue;
                        const NodeId tn = nodeForBit(ctx, t);
                        if (m.collapsed(tn)) { m.stuckRejected.insert(tn); continue; }
                        if (!src.sym) {
                            if (src.value == '0' || src.value == '1')
                                m.stuckCandidates[tn].push_back({Model::StuckFlopWrite::Kind::Const, src.value, kNoNode, uncondWrite});
                            else
                                m.stuckRejected.insert(tn);
                        } else {
                            const NodeId sn = nodeForBit(ctx, src);
                            m.stuckCandidates[tn].push_back({Model::StuckFlopWrite::Kind::Copy, '?', sn, uncondWrite});
                        }
                    }
                } else {
                    bool handled = false;
                    if (a.asg && !a.asg->isCompound() && liveModule.size() == 1 && a.targets.size() == 1 &&
                        widthOf(*liveModule[0].sym) == 1 &&
                        (a.asg->left().kind == ExpressionKind::NamedValue || a.asg->left().kind == ExpressionKind::HierarchicalValue) &&
                        a.asg->left().getSymbolReference() == liveModule[0].sym) {
                        auto strip1 = [](const Expression& e) -> const Expression& {
                            const Expression* x = &e;
                            while (x->kind == ExpressionKind::Conversion && x->type && x->as<ConversionExpression>().operand().type &&
                                   x->type->getBitstreamWidth() == 1 && x->as<ConversionExpression>().operand().type->getBitstreamWidth() == 1)
                                x = &x->as<ConversionExpression>().operand();
                            return *x;
                        };
                        const Expression& rhs = strip1(a.asg->right());
                        if (rhs.kind == ExpressionKind::BinaryOp && rhs.type && rhs.type->isIntegral() && rhs.type->getBitstreamWidth() == 1) {
                            const auto& bin = rhs.as<BinaryExpression>();
                            using Kind = Model::StuckFlopWrite::Kind;
                            std::optional<Kind> kind;
                            if (bin.op == BinaryOperator::BinaryXor) kind = Kind::SelfXor;
                            else if (bin.op == BinaryOperator::BinaryOr || bin.op == BinaryOperator::LogicalOr) kind = Kind::SelfOr;
                            else if (bin.op == BinaryOperator::BinaryAnd || bin.op == BinaryOperator::LogicalAnd) kind = Kind::SelfAnd;
                            if (kind) {
                                const Expression& l = strip1(bin.left());
                                const Expression& r = strip1(bin.right());
                                auto isSelfWhole = [&](const Expression& e) {
                                    return (e.kind == ExpressionKind::NamedValue || e.kind == ExpressionKind::HierarchicalValue) &&
                                           e.getSymbolReference() == liveModule[0].sym;
                                };
                                const bool lSelf = isSelfWhole(l);
                                const bool rSelf = isSelfWhole(r);
                                if (lSelf != rSelf) {
                                    const Expression& otherExpr = lSelf ? r : l;
                                    const NodeId tn = nodeForBit(ctx, BitRef{liveModule[0].sym, static_cast<uint32_t>(liveModule[0].lo), 0});
                                    auto recordConstOther = [&](char cvBit) {
                                        if (*kind == Kind::SelfXor) {
                                            if (cvBit == '0') { m.stuckCandidates[tn].push_back({Kind::Copy, '?', tn, uncondWrite}); return true; }
                                            return false;
                                        }
                                        if (*kind == Kind::SelfOr) {
                                            m.stuckCandidates[tn].push_back(cvBit == '0'
                                                ? Model::StuckFlopWrite{Kind::Copy, '?', tn, uncondWrite}
                                                : Model::StuckFlopWrite{Kind::Const, '1', kNoNode, uncondWrite});
                                            return true;
                                        }
                                        // SelfAnd
                                        m.stuckCandidates[tn].push_back(cvBit == '1'
                                            ? Model::StuckFlopWrite{Kind::Copy, '?', tn, uncondWrite}
                                            : Model::StuckFlopWrite{Kind::Const, '0', kNoNode, uncondWrite});
                                        return true;
                                    };
                                    if (!m.collapsed(tn) && callsIn(otherExpr).empty()) {
                                        const auto cv = otherExpr.eval(ec);
                                        if (cv.isInteger() && !cv.hasUnknown() && cv.integer().getBitWidth() >= 1) {
                                            handled = recordConstOther(cv.isTrue() ? '1' : '0');
                                        } else if ((otherExpr.kind == ExpressionKind::NamedValue || otherExpr.kind == ExpressionKind::HierarchicalValue) &&
                                                   otherExpr.type && otherExpr.type->getBitstreamWidth() == 1) {
                                            auto ps = pathsOf(otherExpr, ec, false);
                                            if (ps.size() == 1 && pathWidth(ps[0]) == 1) {
                                                auto ns = nodesFor(ctx, ps[0]);
                                                if (ns.size() == 1 && !m.collapsed(ns[0]) && ns[0] != tn) {
                                                    m.stuckCandidates[tn].push_back({*kind, '?', ns[0], uncondWrite});
                                                    handled = true;
                                                }
                                            } else if (ps.empty() && design.hasCaseMode()) {
                                                const int mv = modeValue(otherExpr, ec);
                                                if (mv == 0 || mv == 1) handled = recordConstOther(mv ? '1' : '0');
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!handled) rejectPaths(liveModule);
                }
            }
            if (!deadLocal.empty() || !deadModule.empty())
                deferredDead.push_back({&a, std::move(deadLocal), std::move(deadModule)});
        }
        curDeadWrite = true;
        for (const auto& d : deferredDead) {
            if (!d.localTargets.empty()) emitAssign(ctx, *d.a, d.localTargets, EdgeType::Comb, proc, pb.location);
            if (!d.moduleTargets.empty()) {
                emitAssign(ctx, *d.a, d.moduleTargets, EdgeType::Seq, proc, pb.location, &resetNodes);
                emitModuleExtras(*d.a, d.moduleTargets);
            }
        }
        curDeadWrite = false;
        for (NodeId q : qs) {
            const auto* sym = m.nodes[q].sym;
            if (procHasUnattributedWrite || procHasBodyTiming || !sym || m.collapsed(q) || blockingState.count(sym) || anyEventHasIff || m.flopOfQ.count(q))
                m.stuckRejected.insert(q);
            else if (const auto* var = sym->as_if<VariableSymbol>()) {
                const auto* init = [&]() -> const Expression* {
                    if (const auto* i = var->getInitializer()) return i;
                    if (const auto* scope = var->getParentScope(); scope && scope->asSymbol().kind == SymbolKind::InstanceBody)
                        for (const auto* symbol : scope->asSymbol().as<InstanceBodySymbol>().getPortList())
                            if (const auto* port = symbol->as_if<PortSymbol>(); port && port->isAnsiPort &&
                                port->direction == ArgumentDirection::Out && port->internalSymbol == var)
                                return port->getInitializer();
                    return nullptr;
                }();
                if (init) {
                    const ConstantValue cv = var->getParentScope()
                        ? [&] { EvalContext initEc(*var); return init->eval(initEc); }()
                        : init->eval(ec);
                    if (!cv.isInteger() || cv.hasUnknown() || m.nodes[q].bit >= cv.integer().getBitWidth())
                        m.stuckRejected.insert(q);
                    else
                        m.stuckInitValues[q] = cv.integer()[static_cast<int32_t>(m.nodes[q].bit)] ? '1' : '0';
                }
            }
            if (m.flopOfQ.count(q)) {
                m.multiSequential.insert(q);
                if (clockOk && clockNode != kNoNode) m.flops[m.flopOfQ.at(q)].otherClockSignals.push_back(clockNode);
                fallback("multiple-sequential-drivers",
                         "register '" + m.nameOf(q) + "' is assigned by more than one sequential block; every block's clock "
                         "is weighed, but what the register holds at any time is not modelled", pb.location);
                continue;
            }
            Flop f;
            f.q = q;
            f.proc = proc;
            f.clockSignal = clockOk ? clockNode : kNoNode;
            f.clockResolved = clockOk && clockNode != kNoNode;
            f.edge = clockEdge;
            f.hasIff = anyEventHasIff;
            // Per bit: 'n' = this control's branch does not assign the bit, 'h' = it assigns the bit its own value (held during
            // the control). Both stay listed as resets (CDC reads them as unknown forced values); RDC decides what holds.
            f.resets = resets;
            for (size_t ri = 0; ri < resets.size(); ri++) {
                auto it = controlKinds[ri].find(m.nodes[q].sym);
                char ch = unknownForced[ri] ? '?' : 'n';   // 'n': this reset branch does not assign the bit
                if (!unknownForced[ri] && it != controlKinds[ri].end()) {
                    const std::string& bits = it->second;
                    uint32_t bit = m.nodes[q].bit;
                    if (m.collapsed(q)) {   // one node stands for the whole vector: a value only if every bit agrees
                        ch = bits.empty() ? 'n' : bits[0];
                        for (char c : bits) if (c != ch) ch = '?';
                    }
                    else if (bit < bits.size()) ch = bits[bit];
                    if (ch == '\0') ch = 'n';
                }
                // A write under a compound/multiply-driven guard or a nested test holds when that condition fails:
                // the forced value is not a guarantee (round 4 A2/A2a), so this bit is undecided.
                const bool bitNested = ri < nestedTargets.size() && nestedTargets[ri].count(q) > 0;
                const bool bitConditional = (ri < controlConditional.size() && controlConditional[ri]) || bitNested;
                if (ch != 'n' && bitConditional) ch = '?';
                f.resetValues.push_back(ch);
                f.resetKinds.push_back(ch == '0' ? 0 : ch == '1' ? 1 : 2);
                f.resetConditional.push_back(bitConditional);
            }
            if (auto it = transitions.find(q); it != transitions.end()) {
                f.resetTransitionKnown = true;
                f.resetValues[0] = it->second.asserted;
                f.resetKinds[0] = it->second.asserted == '1' ? 1 : 0;
                const auto& src = it->second.released;
                if (src.sym) f.releasedSource = nodeForBit(ctx, src);
                else f.releasedValue = src.value;
            }
            auto trailing = trailingResetGuards.find(q);
            auto trailingCand = trailingSyncResetCandidates.find(q);
            auto syncForce = syncResetValues.find(q);
            bool syncBit = syncCand && syncForce != syncResetValues.end() &&
                (syncForce->second == '0' || syncForce->second == '1');
            std::vector<SyncResetGuard> guardFacts;
            if (trailing != trailingResetGuards.end()) {
                for (const auto& [guardPolarity, forced] : trailing->second) {
                    guardFacts.push_back({guardPolarity.first, guardPolarity.second, forced});
                }
            }
            if (syncBit) for (const auto& [guard, low] : syncGuardPolarity)
                guardFacts.push_back({guard, low, syncForce->second});
            std::sort(guardFacts.begin(), guardFacts.end());
            guardFacts.erase(std::unique(guardFacts.begin(), guardFacts.end()), guardFacts.end());
            std::vector<SyncResetGuard> facts = guardFacts;
            if (trailingCand != trailingSyncResetCandidates.end()) {
                for (const auto& [guardPolarity, forced] : trailingCand->second) {
                    facts.push_back({guardPolarity.first, guardPolarity.second, forced});
                }
            }
            std::sort(facts.begin(), facts.end());
            facts.erase(std::unique(facts.begin(), facts.end()), facts.end());
            f.hasSyncResetCandidate = !facts.empty();
            f.syncResetFacts = facts;
            for (const auto& br : extraGuardBranches) {
                if (br.nested.count(q)) continue;
                auto it = br.bits.find(m.nodes[q].sym);
                if (it == br.bits.end()) continue;
                const std::string& bits = it->second;
                char ch = 'n';
                if (m.collapsed(q)) {
                    ch = bits.empty() ? 'n' : bits[0];
                    for (char c : bits) if (c != ch) ch = '?';
                } else if (m.nodes[q].bit < bits.size()) {
                    ch = bits[m.nodes[q].bit];
                }
                if (ch == '0' || ch == '1') {
                    for (const auto& [g, low] : br.guards)
                        f.extraResetGuards.push_back({g, low, ch});
                }
            }
            if (!f.extraResetGuards.empty()) {
                std::sort(f.extraResetGuards.begin(), f.extraResetGuards.end());
                f.extraResetGuards.erase(std::unique(f.extraResetGuards.begin(), f.extraResetGuards.end()), f.extraResetGuards.end());
            }
            for (const auto& fact : guardFacts) f.syncResetGuards.push_back(fact.node);
            f.syncReset = facts.empty() ? kNoNode : facts.front().node;
            f.syncActiveLow = facts.empty() ? syncLow : facts.front().activeLow;
            f.syncDataSuppressed = unconditionalOverrideBits.count(q) != 0;
            f.blocking = blockingState.count(m.nodes[q].sym) > 0;
            m.flopOfQ[q] = static_cast<uint32_t>(m.flops.size());
            m.flops.push_back(std::move(f));
        }
    }

    // ---------------------------------------------------------------- continuous assigns / nets
    void handleAssignExpr(const Expression& e, const Symbol& anchor, const InstanceSymbol* ctx, slang::SourceLocation loc) {
        EvalContext ec(anchor);
        if (e.kind != ExpressionKind::Assignment) return;
        std::vector<Assign> as;
        StmtVisitor v(*this, ec, as, ctx);
        e.visit(v);
        uint32_t proc = addProc(ProcKind::Continuous, loc, ctx);
        if (anchor.kind == SymbolKind::ContinuousAssign) m.procs[proc].delayed = anchor.as<ContinuousAssignSymbol>().getDelay() != nullptr;
        struct PairFacts : ASTVisitor<PairFacts, VisitFlags::AllGood> {
            Model& model;
            uint32_t proc;
            explicit PairFacts(Model& model, uint32_t proc) : model(model), proc(proc) {}
            void handle(const BinaryExpression& b) {
                PairOperator op;
                switch (b.op) {
                    case BinaryOperator::Equality: op = PairOperator::Equal; break;
                    case BinaryOperator::Inequality: op = PairOperator::NotEqual; break;
                    case BinaryOperator::BinaryXor: op = PairOperator::Xor; break;
                    case BinaryOperator::BinaryAnd: op = PairOperator::And; break;
                    default: visitDefault(b); return;
                }
                const auto ref = [](const Expression& x) -> const ValueSymbol* {
                    if (x.kind != ExpressionKind::NamedValue && x.kind != ExpressionKind::HierarchicalValue) return nullptr;
                    auto* s = x.getSymbolReference();
                    return s && s->isValue() && widthOf(s->as<ValueSymbol>()) == 1 ? &s->as<ValueSymbol>() : nullptr;
                };
                auto* left = ref(b.left()); auto* right = ref(b.right());
                if (left && right && left != right) model.pairOperatorFacts.push_back({proc, left, right, op});
                visitDefault(b);
            }
        } pairs(m, proc);
        e.visit(pairs);
        const Expression* rhs = &e.as<AssignmentExpression>().right();
        const auto cf = foldConstantExpr(*rhs, ec);
        if (cf == ConstFold::None)
            m.pairOperatorFacts.push_back({proc, nullptr, nullptr, PairOperator::And, rhs, &anchor});
        else {
            m.constantProcs.insert(proc);
            if (cf == ConstFold::Zero || cf == ConstFold::AllOnes || cf == ConstFold::OneBitTrue) {
                const char bitVal = (cf == ConstFold::Zero) ? '0' : '1';
                for (const auto& a : as) {
                    for (const auto& t : a.targets) {
                        auto targetNodes = nodesFor(ctx, t);
                        if (cf == ConstFold::OneBitTrue && targetNodes.size() != 1) continue;
                        for (auto tn : targetNodes)
                            m.foldedConstDrivers[tn].push_back({bitVal, EdgeType::Comb, proc});
                    }
                }
            }
        }
        for (auto& a : as) {
            emitAssign(ctx, a, a.targets, EdgeType::Comb, proc, loc);
        }
        const auto& ca = e.as<AssignmentExpression>();
        if (!ca.isCompound() && !hasDynamicSelect(ca.left(), ec) && callsIn(ca.left()).empty()) {
            std::vector<BitRef> targetBits;
            if (bitList(ca.left(), ec, targetBits) && !targetBits.empty() &&
                std::all_of(targetBits.begin(), targetBits.end(), [](const BitRef& bit) { return bit.sym != nullptr && !bit.sym->getType().isUnpackedArray(); }) &&
                !collapse(*targetBits.front().sym, widthOf(*targetBits.front().sym))) {
                auto arms = decomposeExprArms(ca.right(), ec, ctx, targetBits.size());
                if (arms.size() == targetBits.size()) {
                    for (size_t i = 0; i < targetBits.size(); ++i) {
                        CombQualifierSummary summary;
                        summary.supported = true;
                        summary.arms = std::move(arms[i]);
                        recordCombDriver(nodeForBit(ctx, targetBits[i]), proc, std::move(summary));
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- hierarchy
    void collectInstances(const InstanceSymbol& inst, bool isTop = true) {
        if (!isTop) m.subInstancePaths.push_back(inst.getHierarchicalPath());
        bodyInstances[&inst.body].push_back(&inst);
        collectScope(inst.body);
    }
    void collectScope(const Scope& sc) {
        for (auto& mem : sc.members()) {
            switch (mem.kind) {
                case SymbolKind::Instance: collectInstances(mem.as<InstanceSymbol>(), false); break;
                case SymbolKind::InstanceArray: collectArray(mem.as<InstanceArraySymbol>()); break;
                case SymbolKind::GenerateBlock:
                    if (!mem.as<GenerateBlockSymbol>().isUninstantiated) collectScope(mem.as<GenerateBlockSymbol>());
                    break;
                case SymbolKind::GenerateBlockArray:
                    for (auto* e : mem.as<GenerateBlockArraySymbol>().entries)
                        if (!e->isUninstantiated) collectScope(*e);
                    break;
                default: break;
            }
        }
    }
    void collectArray(const InstanceArraySymbol& arr) {
        for (auto* e : arr.elements) {
            if (e->kind == SymbolKind::Instance) collectInstances(e->as<InstanceSymbol>(), false);
            else if (e->kind == SymbolKind::InstanceArray) collectArray(e->as<InstanceArraySymbol>());
        }
    }

    void walkInstance(const InstanceSymbol& inst) {
        m.numInstances++;
        processScope(inst.body, &inst);
    }
    void walkArray(const InstanceArraySymbol& arr, const InstanceSymbol* ctx) {
        for (auto* e : arr.elements) {
            if (e->kind == SymbolKind::Instance) {
                if (handleLibCell(e->as<InstanceSymbol>(), ctx) || handleAttributedBlackBox(e->as<InstanceSymbol>(), ctx)) continue;
                connectPorts(e->as<InstanceSymbol>(), ctx);
                walkInstance(e->as<InstanceSymbol>());
            }
            else if (e->kind == SymbolKind::InstanceArray) walkArray(e->as<InstanceArraySymbol>(), ctx);
        }
    }

    void processScope(const Scope& scope, const InstanceSymbol* ctx) {
        // Count every driver in the scope before recording plain copies: a multiply-driven net (`wand alias_n`) or delayed copy
        // is never an exact alias, though its plain-copy sources stay available for reset-event identification (round 4 A2a).
        std::unordered_map<const ValueSymbol*, int> driverCount;
        struct CopyCand { const ValueSymbol* lhs; const ValueSymbol* rhs; bool delayed; };
        std::vector<CopyCand> copyCands;
        for (auto& mem : scope.members()) {
            if (mem.kind == SymbolKind::ContinuousAssign) {
                const auto& ca = mem.as<ContinuousAssignSymbol>();
                const Expression& e = ca.getAssignment();
                if (e.kind == ExpressionKind::Assignment) {
                    const auto& a = e.as<AssignmentExpression>();
                    EvalContext ec(ca);
                    for (const auto& p : rawPathsOf(a.left(), ec, true)) driverCount[p.sym]++;
                    if (!a.isCompound() && a.left().kind == ExpressionKind::NamedValue && a.right().kind == ExpressionKind::NamedValue) {
                        const auto* lhs = &a.left().as<NamedValueExpression>().symbol;
                        const auto* rhs = &a.right().as<NamedValueExpression>().symbol;
                        const bool netDelay = lhs->kind == SymbolKind::Net && lhs->as<NetSymbol>().getDelay() != nullptr;
                        copyCands.push_back({lhs, rhs, ca.getDelay() != nullptr || netDelay});
                    }
                }
            } else if (mem.kind == SymbolKind::Net) {
                const auto& net = mem.as<NetSymbol>();
                if (const Expression* init = net.getInitializer()) {
                    driverCount[&net]++;
                    if (init->kind == ExpressionKind::NamedValue)
                        copyCands.push_back({&net, &init->as<NamedValueExpression>().symbol, net.getDelay() != nullptr});
                }
            } else if (mem.kind == SymbolKind::Instance) {
                const auto& inst = mem.as<InstanceSymbol>();
                for (const auto* pc : inst.getPortConnections()) {
                    if (!pc->getExpression()) continue;
                    const auto* port = pc->port.as_if<PortSymbol>();
                    if (!port || port->direction == ArgumentDirection::In) continue;
                    EvalContext ec(inst);
                    for (const auto& p : rawPathsOf(*pc->getExpression(), ec, true)) driverCount[p.sym]++;
                }
            } else if (mem.kind == SymbolKind::PrimitiveInstance) {
                const auto& prim = mem.as<PrimitiveInstanceSymbol>();
                EvalContext ec(prim);
                for (const auto* conn : prim.getPortConnections()) {
                    if (!conn) continue;
                    for (const auto& p : rawPathsOf(*conn, ec, true)) driverCount[p.sym]++;
                }
            }
        }
        for (const auto& c : copyCands) {
            const bool multi = driverCount[c.lhs] > 1;
            if (!multi && !c.delayed) addPlainCopy(c.lhs, c.rhs);
            else {
                multiDrivenSyms.insert(c.lhs);
                auto& srcs = multiDriverSources[c.lhs];
                if (std::find(srcs.begin(), srcs.end(), c.rhs) == srcs.end()) srcs.push_back(c.rhs);
            }
        }
        for (auto& mem : scope.members()) {
            switch (mem.kind) {
                case SymbolKind::ProceduralBlock: handleProcedure(mem.as<ProceduralBlockSymbol>(), ctx); break;
                case SymbolKind::ContinuousAssign: {
                    auto& ca = mem.as<ContinuousAssignSymbol>();
                    handleAssignExpr(ca.getAssignment(), ca, ctx, ca.location);
                    inventoryReads(ca, ctx, ca.getAssignment());
                    break;
                }
                case SymbolKind::Net: {
                    auto& net = mem.as<NetSymbol>();
                    if (auto* init = net.getInitializer()) {
                        inventoryReads(net, ctx, *init);
                        // `wire w = expr;` is a continuous assignment to w
                        EvalContext ec(net);
                        std::vector<Path> reads = pathsOf(*init, ec, false);
                        Path t{&net, 0, widthOf(net) ? widthOf(net) - 1 : 0};
                        uint32_t proc = addProc(ProcKind::Continuous, net.location, ctx);
                        if (net.getDelay() != nullptr) m.procs[proc].delayed = true;
                        Assign as;
                        as.targets = {t};
                        as.dataReads = reads;
                        ConditionalReads conditions(*this, ec, as);
                        init->visit(conditions);
                        classify(modeExpr(*init, ec), Op::None, 0, as.dataOps);
                        as.booleanValue = booleanOf(*init, ec, ctx);
                        as.uncertainCalls = callIssues(*init, ctx);
                        std::vector<BitRef> R;
                        uint64_t w = widthOf(net);
                        if (w && bitList(*init, ec, R) && R.size() == w) {
                            as.pairsValid = true;
                            for (uint64_t i = 0; i < w; i++) as.pairs.push_back({{&net, static_cast<uint32_t>(w - 1 - i)}, R[i]});
                        } else if (std::vector<BitRef> I; w && invertedBitMove(*init, ec, I) && I.size() == w) {
                            // `wire c = ~x;` is the same copy through an inversion as `assign c = ~x;`
                            as.flipPairsValid = true;
                            for (uint64_t i = 0; i < w; i++) as.pairs.push_back({{&net, static_cast<uint32_t>(w - 1 - i)}, I[i]});
                        }
                        emitAssign(ctx, as, as.targets, EdgeType::Comb, proc, net.location);
                        const auto cf = foldConstantExpr(*init, ec);
                        if (cf == ConstFold::None)
                            m.pairOperatorFacts.push_back({proc, nullptr, nullptr, PairOperator::And, init, &net});
                        else {
                            m.constantProcs.insert(proc);
                            if (cf == ConstFold::Zero || cf == ConstFold::AllOnes || (cf == ConstFold::OneBitTrue && w == 1)) {
                                const char bitVal = (cf == ConstFold::Zero) ? '0' : '1';
                                for (auto tn : nodesFor(ctx, t))
                                    m.foldedConstDrivers[tn].push_back({bitVal, EdgeType::Comb, proc});
                            }
                        }
                        if (w && !net.getType().isUnpackedArray() && !collapse(net, w)) {
                            auto arms = decomposeExprArms(*init, ec, ctx, w);
                            if (arms.size() == w) {
                                for (uint64_t i = 0; i < w; ++i) {
                                    CombQualifierSummary summary;
                                    summary.supported = true;
                                    summary.arms = std::move(arms[i]);
                                    recordCombDriver(nodeForBit(ctx, {&net, static_cast<uint32_t>(w - 1 - i)}), proc, std::move(summary));
                                }
                            }
                        }
                    }
                    break;
                }
                case SymbolKind::Instance: {
                    auto& ci = mem.as<InstanceSymbol>();
                    if (handleLibCell(ci, ctx) || handleAttributedBlackBox(ci, ctx)) break;
                    connectPorts(ci, ctx);
                    walkInstance(ci);
                    break;
                }
                case SymbolKind::InstanceArray: walkArray(mem.as<InstanceArraySymbol>(), ctx); break;
                case SymbolKind::UninstantiatedDef: handleBlackBox(mem.as<UninstantiatedDefSymbol>(), ctx); break;
                case SymbolKind::PrimitiveInstance: handlePrimitive(mem.as<PrimitiveInstanceSymbol>(), ctx); break;
                case SymbolKind::ClockingBlock:
                case SymbolKind::CheckerInstance:
                    fallback("unsupported-member", "member '" + std::string(mem.name) + "' of kind " +
                             std::string(toString(mem.kind)) + " is not modelled; signals it drives or reads may be missing "
                             "from the dependency graph", mem.location);
                    break;
                case SymbolKind::GenerateBlock:
                    if (!mem.as<GenerateBlockSymbol>().isUninstantiated) processScope(mem.as<GenerateBlockSymbol>(), ctx);
                    break;
                case SymbolKind::GenerateBlockArray:
                    for (auto* e : mem.as<GenerateBlockArraySymbol>().entries)
                        if (!e->isUninstantiated) {
                            const bool saved = loopGenerate;
                            loopGenerate = true;
                            processScope(*e, ctx);
                            loopGenerate = saved;
                        }
                    break;
                default: break;
            }
        }
    }

    /// Gate-level primitives: combinational dependency from inputs to outputs (inexact). Anything not recognised
    /// is connected conservatively in all directions and recorded as a fallback.
    void handlePrimitive(const PrimitiveInstanceSymbol& pi, const InstanceSymbol* ctx) {
        EvalContext ec(pi);
        auto conns = pi.getPortConnections();
        std::string kind(pi.primitiveType.name);
        std::vector<std::vector<Path>> paths(conns.size());
        for (size_t i = 0; i < conns.size(); i++) {
            const Expression* x = conns[i];
            if (x->kind == ExpressionKind::Assignment) x = &x->as<AssignmentExpression>().left();
            paths[i] = pathsOf(*x, ec, false);
        }
        uint32_t proc = addProc(ProcKind::Comb, pi.location, ctx);
        std::set<size_t> outs, ins;
        // Terminal 0 is the output; every other terminal (data, enable or control) feeds it. The switch-level and tri-state
        // gates are modelled the same way: a bufif's output depends on its data and enable, a MOS switch's on its data and gates.
        static const std::set<std::string_view> kInputsToFirst = {"and", "nand", "or", "nor", "xor", "xnor", "bufif0", "bufif1", "notif0", "notif1",
                                                                  "nmos", "pmos", "rnmos", "rpmos", "cmos", "rcmos"};
        bool approximate = false;
        if (kInputsToFirst.count(kind)) {
            outs.insert(0);
            for (size_t i = 1; i < conns.size(); i++) ins.insert(i);
        } else if ((kind == "buf" || kind == "not") && !conns.empty()) {
            for (size_t i = 0; i + 1 < conns.size(); i++) outs.insert(i);
            ins.insert(conns.size() - 1);
        } else {
            // tran-family switches are bidirectional and a user-defined primitive's table is not read: every terminal depends on
            // every other, and those edges are approximate, so a loop that needs one is Unresolved, never a proven cycle.
            fallback("primitive", "primitive '" + kind + "' is not modelled precisely; all connections are treated as "
                     "mutually dependent", pi.location);
            approximate = true;
            for (size_t i = 0; i < conns.size(); i++) { outs.insert(i); ins.insert(i); }
        }
        curApproximate = approximate;
        struct Reset { Builder& b; ~Reset() { b.curApproximate = false; } } reset{*this};
        for (auto o : outs)
            for (auto i : ins) {
                if (o == i) continue;
                // a `buf` of one bit to one bit is a plain copy
                std::vector<NodeId> from, to;
                if (kind == "buf" && paths[i].size() == 1 && paths[o].size() == 1) { from = nodesFor(ctx, paths[i][0]); to = nodesFor(ctx, paths[o][0]); }
                if (from.size() == 1 && to.size() == 1) addEdge(from[0], to[0], EdgeType::Comb, proc, true);
                else connect(ctx, paths[i], ctx, paths[o], EdgeType::Comb, proc, pi.location);
                auto issues = callIssues(*conns[i], ctx);
                for (const auto& path : paths[o]) for (auto node : nodesFor(ctx, path)) {
                    auto& targetIssues = m.uncertainCallDrivers[node];
                    targetIssues.insert(targetIssues.end(), issues.begin(), issues.end());
                }
            }
    }

    /// A declared library cell (--library). Its body is not walked; it is modelled by kind:
    ///  synchronizer / flop: its output bits become registers on the declared clock (flop: optional asynchronous reset with value),
    ///  clock_gate: the gated clock is an exact alias of the clock (clock tracing passes through) and the enable is recorded,
    ///  ram: write and read clocks recorded, read data marked as RAM-driven,
    ///  blackbox: ports with declared directions and roles; outputs are black-box driven.
    bool handleLibCell(const InstanceSymbol& ci, const InstanceSymbol* ctx) {
        auto it = design.libCells.find(std::string(ci.getDefinition().name));
        if (it == design.libCells.end()) return false;
        const LibCell& cell = it->second;
        EvalContext ec(ci);
        auto declaredOut = [&](const std::string& pn) {
            if (cell.kind == "synchronizer" || cell.kind == "flop" || cell.kind == "clock_gate") return pn == cell.output;
            for (auto& p : cell.ports) if (p.name == pn) return p.dir != "in";
            return false;
        };
        std::vector<uint32_t> connectionIssues;
        std::map<std::string, std::vector<NodeId>> conn;
        std::set<std::string> simpleScalarConnections, conjunctiveConnections;
        for (auto* pc : ci.getPortConnections()) {
            if (!pc || !pc->getExpression()) continue;
            auto issues = callIssues(*pc->getExpression(), ctx);
            connectionIssues.insert(connectionIssues.end(), issues.begin(), issues.end());
            std::string pn(pc->port.name);
            const auto& expr = pc->getExpression()->unwrapImplicitConversions();
            if (expr.kind == ExpressionKind::NamedValue && expr.type->getBitstreamWidth() == 1)
                simpleScalarConnections.insert(pn);
            if (expr.kind == ExpressionKind::BinaryOp && expr.type->getBitstreamWidth() == 1) {
                const auto op = expr.as<BinaryExpression>().op;
                if (op == BinaryOperator::BinaryAnd || op == BinaryOperator::LogicalAnd)
                    conjunctiveConnections.insert(pn);
            }
            for (auto& p : pathsOf(*pc->getExpression(), ec, declaredOut(pn)))
                for (auto n : nodesFor(ctx, p)) conn[pn].push_back(n);
        }
        auto get = [&](const std::string& pn) -> const std::vector<NodeId>& {
            static const std::vector<NodeId> none;
            auto f = conn.find(pn);
            return f == conn.end() ? none : f->second;
        };
        if (!connectionIssues.empty()) for (const auto& [name, nodes] : conn) if (declaredOut(name))
            for (auto node : nodes) {
                auto& issues = m.uncertainCallDrivers[node];
                issues.insert(issues.end(), connectionIssues.begin(), connectionIssues.end());
            }
        auto roleOf = [](const std::string& r) {
            if (r == "clock") return PortRole::Clock;
            if (r == "reset") return PortRole::Reset;
            if (r == "enable") return PortRole::Enable;
            if (r == "data_in") return PortRole::DataIn;
            if (r == "data_out") return PortRole::DataOut;
            if (r == "address") return PortRole::Address;
            if (r == "write_enable") return PortRole::WriteEnable;
            if (r == "gated_clock") return PortRole::GatedClock;
            return PortRole::Unknown;
        };
        auto dirOf = [](const std::string& d) { return d == "in" ? PortDir::In : d == "out" ? PortDir::Out : d == "inout" ? PortDir::InOut : PortDir::Unknown; };
        std::vector<BlackBoxPort> ports;
        auto addPort = [&](const std::string& name, PortDir d, PortRole r, const std::string& clk = {}) { ports.push_back({name, d, r, clk, get(name)}); };
        const std::string& kind = cell.kind;
        bool ok = true;
        if (kind == "synchronizer" || kind == "flop") ok = !get(cell.input).empty() && !get(cell.output).empty() && !get(cell.clock).empty();
        else if (kind == "clock_gate") ok = !get(cell.clock).empty() && !get(cell.enable).empty() && !get(cell.output).empty();
        else if (kind == "ram") {
            // Empty connections are meaningful CDC-035 evidence. Retain the
            // RAM so missing clocks and other connected port crossings survive.
            ok = true;
        }
        if (!ok) {
            fallback("library-cell", "library cell '" + cell.name + "' instance '" + std::string(ci.name) + "' lacks a connection for a declared port; treated as a black box", ci.location);
            return false;
        }
        uint32_t proc = addProc(kind == "clock_gate" ? ProcKind::Continuous : ProcKind::Sequential, ci.location, &ci);
        if (kind == "synchronizer" || kind == "flop") {
            const auto &in = get(cell.input), &out = get(cell.output), &clk = get(cell.clock);
            m.numSeqProcedures++;
            for (size_t i = 0; i < out.size(); i++) {
                NodeId q = out[i];
                if (m.flopOfQ.count(q)) continue;
                NodeId src = in.size() == out.size() ? in[i] : in[std::min(i, in.size() - 1)];
                addEdge(src, q, EdgeType::Seq, proc, in.size() == out.size());
                Flop f;
                f.q = q;
                f.proc = proc;
                f.clockSignal = clk[0];
                f.clockResolved = true;
                f.edge = cell.edge;
                f.extraStages = cell.stages - 1;
                if (kind == "flop" && !cell.reset.empty() && !get(cell.reset).empty()) {
                    ResetInfo ri;
                    ri.signal = get(cell.reset)[0];
                    ri.async = true;
                    ri.activeLow = cell.resetActiveLow;
                    ri.fromEdge = false;
                    f.resets.push_back(ri);
                    f.resetValues.push_back(cell.resetValue);
                    f.resetTransitionKnown = true; // declared flop contract
                    f.releasedSource = src;
                    f.resetKinds.push_back(cell.resetValue == '1' ? 1 : 0);
                    addEdge(ri.signal, q, EdgeType::AsyncCtl, proc, false);
                }
                m.flopOfQ[q] = static_cast<uint32_t>(m.flops.size());
                m.flops.push_back(std::move(f));
            }
            addPort(cell.clock, PortDir::In, PortRole::Clock);
            addPort(cell.input, PortDir::In, PortRole::DataIn);
            addPort(cell.output, PortDir::Out, PortRole::DataOut);
            if (!cell.reset.empty()) addPort(cell.reset, PortDir::In, PortRole::Reset);
        }
        else if (kind == "clock_gate") {
            const auto &clk = get(cell.clock), &en = get(cell.enable), &out = get(cell.output);
            for (auto o : out) addEdge(clk[0], o, EdgeType::Comb, proc, true);
            m.clockGates.push_back({cell.name, ci.getHierarchicalPath(), clk, en, out, cell.enableLatched, toLoc(*sm, ci.location)});
            addPort(cell.clock, PortDir::In, PortRole::Clock);
            addPort(cell.enable, PortDir::In, PortRole::Enable);
            addPort(cell.output, PortDir::Out, PortRole::GatedClock);
        }
        else if (kind == "ram") {
            RamInst r;
            r.module = cell.name;
            r.hierarchy = ci.getHierarchicalPath();
            r.writeClk = get(cell.writeClock);
            r.readClk = get(cell.readClock);
            r.syncRead = cell.syncRead;
            r.loc = toLoc(*sm, ci.location);
            for (auto& p : cell.ports) {
                addPort(p.name, dirOf(p.dir), roleOf(p.role), p.clock);
                ports.back().simpleScalarConnection = simpleScalarConnections.count(p.name) && get(p.name).size() == 1;
                ports.back().conjunctiveConnection = conjunctiveConnections.count(p.name);
                if (p.dir != "in" && roleOf(p.role) == PortRole::DataOut)
                    for (auto n : get(p.name)) pendingFlags.push_back({n, 5});
            }
            r.ports = ports;
            m.rams.push_back(std::move(r));
        }
        else {   // blackbox with declared ports
            BlackBoxInst bb;
            bb.module = cell.name;
            bb.hierarchy = ci.getHierarchicalPath();
            bb.loc = toLoc(*sm, ci.location);
            bb.declared = true;
            for (auto& p : cell.ports) {
                addPort(p.name, dirOf(p.dir), roleOf(p.role), p.clock);
                if (p.dir != "in")
                    for (auto n : get(p.name)) pendingFlags.push_back({n, 1});
            }
            bb.ports = ports;
            m.blackBoxes.push_back(std::move(bb));
        }
        m.libInstances.push_back({cell.name, ci.getHierarchicalPath(), kind, cell.stages, toLoc(*sm, ci.location), ports});
        return true;
    }

    /// A module that says it is a black box (`(* black_box *)`, `blackbox`, `syn_black_box`): synthesis replaces it, so
    /// its body, often empty or a simulation model, is not the hardware. Its outputs are black-box driven as for an
    /// unknown module, with the directions its ports declare.
    static bool attributedBlackBox(const DefinitionSymbol& definition, const Scope& scope) {
        for (const auto* attribute : scope.getCompilation().getAttributes(definition)) {
            std::string name(attribute->name);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const auto& value = attribute->getValue();
            if ((name == "black_box" || name == "blackbox" || name == "syn_black_box") && !(value.isInteger() && value.integer() == 0))
                return true;
        }
        return false;
    }
    bool handleAttributedBlackBox(const InstanceSymbol& ci, const InstanceSymbol* ctx) {
        const auto& definition = ci.getDefinition();
        if (!attributedBlackBox(definition, *ci.getParentScope())) return false;
        BlackBoxInst bb;
        bb.module = std::string(definition.name);
        bb.hierarchy = ci.getHierarchicalPath();
        bb.loc = toLoc(*sm, ci.location);
        bb.attributed = true;
        EvalContext ec(ci);
        for (auto* pc : ci.getPortConnections()) {
            if (!pc || !pc->getExpression()) continue;
            BlackBoxPort port;
            port.name = std::string(pc->port.name);
            const auto direction = pc->port.kind == SymbolKind::Port ? pc->port.as<PortSymbol>().direction : ArgumentDirection::InOut;
            port.dir = direction == ArgumentDirection::In ? PortDir::In : direction == ArgumentDirection::Out ? PortDir::Out : PortDir::InOut;
            const bool driven = port.dir != PortDir::In;
            for (auto& path : pathsOf(*pc->getExpression(), ec, driven))
                for (auto n : nodesFor(ctx, path)) {
                    if (driven) pendingFlags.push_back({n, 1});
                    port.nodes.push_back(n);
                }
            bb.ports.push_back(std::move(port));
        }
        m.blackBoxes.push_back(std::move(bb));
        m.numInstances++;
        return true;
    }

    void handleBlackBox(const UninstantiatedDefSymbol& ub, const InstanceSymbol* ctx) {
        BlackBoxInst bb;
        bb.module = std::string(ub.definitionName);
        bb.hierarchy = ub.getHierarchicalPath();
        bb.loc = toLoc(*sm, ub.location);
        // The module is unknown: every connected signal may be driven by it. Mark them as black-box
        // driven (conservative) so clock/reset/data tracing reports BlackBox rather than inventing a driver.
        EvalContext ec(ub);
        auto names = ub.getPortNames();
        size_t idx = 0;
        for (auto* conn : ub.getPortConnections()) {
            size_t i = idx++;
            if (!conn || conn->kind != AssertionExprKind::Simple) continue;
            const Expression& e = conn->as<SimpleAssertionExpr>().expr;
            const Expression* x = &e;
            if (x->kind == ExpressionKind::Assignment) x = &x->as<AssignmentExpression>().left();
            BlackBoxPort port;
            port.name = i < names.size() ? std::string(names[i]) : std::to_string(i);
            for (auto& p : pathsOf(*x, ec, true))
                for (auto n : nodesFor(ctx, p)) {
                    pendingFlags.push_back({n, 1});
                    port.nodes.push_back(n);
                }
            bb.ports.push_back(std::move(port));
        }
        m.blackBoxes.push_back(std::move(bb));
    }

    void connectPorts(const InstanceSymbol& ci, const InstanceSymbol* parent) {
        EvalContext ec(ci);
        for (auto* pc : ci.getPortConnections()) {
            if (!pc) continue;
            const Symbol& port = pc->port;
            if (port.kind != SymbolKind::Port) {
                fallback("interface-port", "port '" + std::string(port.name) + "' of instance '" + std::string(ci.name) +
                         "' is an interface/multi port; connectivity through it is not modelled", ci.location);
                continue;
            }
            auto& ps = port.as<PortSymbol>();
            const Expression* e = pc->getExpression();
            if (!e || !ps.internalSymbol || !isGraphSymbol(ps.internalSymbol->as<ValueSymbol>())) continue;
            const ValueSymbol& internal = ps.internalSymbol->as<ValueSymbol>();
            uint64_t iw = widthOf(internal);
            Path ip{&internal, 0, iw ? iw - 1 : 0};
            uint32_t proc = addProc(ProcKind::PortConnection, ci.location, &ci);
            if (ps.direction == ArgumentDirection::Ref) {
                fallback("ref-port", "ref port '" + std::string(port.name) + "' not modelled", ci.location);
                continue;
            }
            bool in = ps.direction == ArgumentDirection::In || ps.direction == ArgumentDirection::InOut;
            bool out = ps.direction == ArgumentDirection::Out || ps.direction == ArgumentDirection::InOut;
            const Expression* act = e;
            if (act->kind == ExpressionKind::Assignment) act = &act->as<AssignmentExpression>().left();
            EvalContext pec(ci);
            auto emitPort = [&](const InstanceSymbol* actCtx, const InstanceSymbol* fmCtx, bool actualIsSource) {
                Assign as;
                std::vector<Path> actual = pathsOf(*act, pec, !actualIsSource);
                as.uncertainCalls = callIssues(*act, actCtx);
                as.approximate = hasDynamicSelect(*act, pec) || !callsIn(*act).empty();
                std::vector<BitRef> A;
                bool listOk = iw && bitList(*act, pec, A) && A.size() == iw;
                if (listOk) {
                    // pair formal bits (MSB first) with actual bits
                    for (uint64_t i = 0; i < iw; i++) {
                        BitRef f{&internal, static_cast<uint32_t>(iw - 1 - i)};
                        if (actualIsSource) as.pairs.push_back({f, A[i]});      // actual -> formal
                        else as.pairs.push_back({A[i], f});                    // formal -> actual
                    }
                    as.pairsValid = true;
                } else if (std::vector<BitRef> I; actualIsSource && iw && invertedBitMove(*act, pec, I) && I.size() == iw) {
                    // `.clr(!rst_ni)`, `.clr(~rst_ni)`: each formal bit is a copy of one actual bit through an inversion
                    for (uint64_t i = 0; i < iw; i++) as.pairs.push_back({BitRef{&internal, static_cast<uint32_t>(iw - 1 - i)}, I[i]});
                    as.flipPairsValid = true;
                } else if (iw > 1 && actualIsSource) {
                    std::vector<std::vector<Path>> deps;
                    uint64_t budget = kMaxEdgesPerAssign;
                    if (bitwiseDeps(*act, pec, deps, budget) && deps.size() == iw) {
                        as.bitDepsValid = true;
                        for (uint64_t i = 0; i < iw; i++)
                            as.bitDeps.push_back({BitRef{&internal, static_cast<uint32_t>(iw - 1 - i)}, std::move(deps[i])});
                    }
                }
                if (actualIsSource) {
                    as.targets = {ip};
                    as.dataReads = actual;
                    OpMap portOps;   // an actual such as ~rst_n arrives inverted at the formal
                    classify(*act, Op::None, 0, portOps);
                    curData = &portOps;
                    emitPortEdges(actCtx, fmCtx, as, proc, ci.location, true);
                    curData = nullptr;
                } else {
                    as.targets = actual;
                    as.dataReads = {ip};
                    emitPortEdges(actCtx, fmCtx, as, proc, ci.location, false);
                }
            };
            if (in) emitPort(parent, &ci, true);
            if (out) emitPort(parent, &ci, false);
        }
    }

    /// Port-connection edges: source side lives in `srcCtx`, target side in `tgtCtx`.
    void emitPortEdges(const InstanceSymbol* parent, const InstanceSymbol* child, const Assign& as, uint32_t proc,
                       slang::SourceLocation loc, bool actualIsSource) {
        const InstanceSymbol* srcCtx = actualIsSource ? parent : child;
        const InstanceSymbol* tgtCtx = actualIsSource ? child : parent;
        curApproximate = as.approximate;
        struct Reset { Builder& b; ~Reset() { b.curApproximate = false; } } reset{*this};
        if (!as.uncertainCalls.empty()) for (const auto& target : as.targets) for (auto node : nodesFor(tgtCtx, target)) {
            auto& issues = m.uncertainCallDrivers[node];
            issues.insert(issues.end(), as.uncertainCalls.begin(), as.uncertainCalls.end());
        }
        if (as.pairsValid) {
            for (auto& [t, src] : as.pairs) {
                if (!src.sym && t.sym && src.value) {
                    const NodeId tn = nodeForBit(tgtCtx, t);
                    noteCombTarget(tn, proc);
                    m.constDrivers[tn].push_back({src.value, EdgeType::Comb, proc});
                }
                if (!src.sym || !t.sym) continue;
                addEdge(nodeForBit(srcCtx, src), nodeForBit(tgtCtx, t), EdgeType::Comb, proc, true);
            }
        } else if (as.flipPairsValid) {
            for (auto& [t, src] : as.pairs) {
                if (!src.sym || !t.sym) continue;
                addEdge(nodeForBit(srcCtx, src), nodeForBit(tgtCtx, t), EdgeType::Comb, proc, false, false, true);
            }
        } else if (as.bitDepsValid) {
            for (const auto& [t, reads] : as.bitDeps) {
                const NodeId target = nodeForBit(tgtCtx, t);
                if (reads.empty()) {
                    noteCombTarget(target, proc);
                    m.constDrivers[target].push_back({'?', EdgeType::Comb, proc});
                }
                for (const auto& read : reads)
                    for (auto source : nodesFor(srcCtx, read)) addEdge(source, target, EdgeType::Comb, proc, false);
            }
        } else {
            connect(srcCtx, as.dataReads, tgtCtx, as.targets, EdgeType::Comb, proc, loc);
        }
    }

    void markTopInputs(const InstanceSymbol& top) {
        for (auto* p : top.body.getPortList()) {
            if (p->kind != SymbolKind::Port) continue;
            auto& ps = p->as<PortSymbol>();
            if (!ps.internalSymbol || !isGraphSymbol(ps.internalSymbol->as<ValueSymbol>())) continue;
            const ValueSymbol& v = ps.internalSymbol->as<ValueSymbol>();
            uint64_t w = widthOf(v);
            bool isIn = ps.direction == ArgumentDirection::In || ps.direction == ArgumentDirection::InOut;
            bool isOut = ps.direction == ArgumentDirection::Out || ps.direction == ArgumentDirection::InOut;
            for (auto n : nodesFor(&top, Path{&v, 0, w ? w - 1 : 0})) {
                if (isIn) pendingFlags.push_back({n, 2});
                if (isOut) pendingFlags.push_back({n, 3});
            }
        }
    }

public:
    void applyFlags() {
        m.blackBoxDriven.assign(m.nodes.size(), 0);
        m.topInput.assign(m.nodes.size(), 0);
        m.topOutput.assign(m.nodes.size(), 0);
        m.unresolvedDriven.assign(m.nodes.size(), 0);
        for (auto& [n, f] : pendingFlags) {
            if (f == 4) m.unresolvedDriven[n] = 1;
            if (f == 1) m.blackBoxDriven[n] = 1;
            if (f == 2) m.topInput[n] = 1;
            if (f == 3) m.topOutput[n] = 1;
            if (f == 5) m.blackBoxDriven[n] = 2;
        }
    }
    friend std::unique_ptr<Model> buildModel(Design&);
};

} // namespace

// ------------------------------------------------------------------- Model methods
std::string Model::nameOf(NodeId id) const {
    if (id == kNoNode || id >= nodes.size()) return "<none>";
    const Node& n = nodes[id];
    if (!n.sym) return "<logic>";
    std::string s;
    if (n.inst) s = n.inst->getHierarchicalPath();
    std::vector<std::string> rel;
    const Scope* sc = n.sym->getParentScope();
    while (sc) {
        const Symbol& ss = sc->asSymbol();
        if (ss.kind == SymbolKind::InstanceBody) break;
        if (ss.kind == SymbolKind::GenerateBlock) {
            const auto& gb = ss.as<GenerateBlockSymbol>();
            if (const SVInt* idx = gb.getArrayIndex()) {
                std::string name;
                if (const Scope* psc = ss.getParentScope(); psc && psc->asSymbol().kind == SymbolKind::GenerateBlockArray) {
                    name = std::string(psc->asSymbol().name);
                    sc = psc;
                } else {
                    name = std::string(ss.name);
                }
                if (!name.empty()) rel.push_back(name + "[" + idx->toString(LiteralBase::Decimal, false) + "]");
                sc = sc->asSymbol().getParentScope();
                continue;
            }
        }
        if (!ss.name.empty()) rel.push_back(std::string(ss.name));
        sc = ss.getParentScope();
    }
    for (auto it = rel.rbegin(); it != rel.rend(); ++it) { s += "."; s += *it; }
    s += "." + std::string(n.sym->name);
    uint64_t w = n.sym->getType().isFixedSize() ? n.sym->getType().getBitstreamWidth() : 0;
    if (w > 1 && !tooWide(n.sym->getType(), w)) s += "[" + std::to_string(n.bit) + "]";
    return s;
}

uint64_t Model::symbolWidth(NodeId id) const {
    if (id == kNoNode || id >= nodes.size() || !nodes[id].sym) return 0;
    const Type& t = nodes[id].sym->getType();
    return t.isFixedSize() ? t.getBitstreamWidth() : 0;
}
bool Model::collapsed(NodeId id) const {
    if (id != kNoNode && id < nodes.size() && !nodes[id].sym) return false;   // a logic hub stands for no signal
    const uint64_t w = symbolWidth(id);
    return w == 0 || tooWide(nodes[id].sym->getType(), w);
}

Root Model::traceRoot(NodeId start) const {
    NodeId cur = start;
    std::unordered_set<NodeId> seen;
    for (;;) {
        if (!seen.insert(cur).second) return {cur, RootKind::Cycle};
        if (cur < blackBoxDriven.size() && blackBoxDriven[cur]) return {cur, RootKind::BlackBox};
        const auto& ie = in[cur];
        if (ie.empty()) return {cur, cur < topInput.size() && topInput[cur] ? RootKind::TopPort : RootKind::Undriven};
        // A net with a constant driver beside its one edge (a wired net held by `1'b0`, say) is not a plain copy of the other driver.
        if (ie.size() == 1 && edges[ie[0]].type == EdgeType::Comb && edges[ie[0]].exact && !constDrivers.count(cur)) { cur = edges[ie[0]].from; continue; }
        // `~~x` is `x`: a net whose only driver inverts one bit of a net whose only driver inverts one bit is a plain copy (the
        // generic clock buffer `inv = ~clk_i; clk_o = ~inv`). One inversion alone is a different signal and stays a root.
        // A collapsed node stands for every bit of its symbol: its single driver may invert a different bit than the one read.
        const auto inversion = [&](NodeId n) -> NodeId {
            if (collapsed(n)) return kNoNode;
            if (n < blackBoxDriven.size() && blackBoxDriven[n]) return kNoNode;
            if (n < unresolvedDriven.size() && unresolvedDriven[n]) return kNoNode;
            const auto& drivers = in[n];
            if (drivers.size() != 1 || constDrivers.count(n)) return kNoNode;
            const Edge& e = edges[drivers[0]];
            return e.type == EdgeType::Comb && e.flip && !e.exact && !e.ctrl && !e.approximate && !e.shared ? e.from : kNoNode;
        };
        if (const NodeId mid = inversion(cur); mid != kNoNode && !seen.count(mid))
            if (const NodeId source = inversion(mid); source != kNoNode) { seen.insert(mid); cur = source; continue; }
        return {cur, RootKind::Logic};
    }
}

void Model::compactEdges() {
    // A signal can be both the data and the enable of one target, and both facts matter: `if (s) t <= s` holds t when s is low,
    // so it is no synchroniser stage. They stay separate edges, and so do an approximate edge and a precise one (a computed index and
    // a constant index of the same signal): merging them would hide a loop that only the precise edge closes. Among repeats of one role, inexact is kept before exact.
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        const auto key = [](const Edge& e) { return std::make_tuple(e.from, e.to, static_cast<int>(e.type), e.proc, e.ctrl, e.approximate, e.exact, e.flip); };
        return key(a) < key(b);
    });
    // A repeat can differ in what it says, as when one condition guards the assignments on both of its branches. Keeping whichever
    // came first would depend on the input order, so the survivor says what is common to all of them, as noteOp does for one assignment.
    size_t kept = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (kept) {
            Edge& last = edges[kept - 1];
            const Edge& e = edges[i];
            if (last.from == e.from && last.to == e.to && last.type == e.type && last.proc == e.proc && last.ctrl == e.ctrl && last.approximate == e.approximate && last.flip == e.flip) {
                if (last.op != e.op) last.op = Op::Other;
                if (last.inv != e.inv) last.inv = 2;
                if (last.branch != e.branch) last.branch = Branch::None;
                last.internalRead = last.internalRead && e.internalRead;
                last.shared = last.shared || e.shared;
                continue;
            }
        }
        edges[kept++] = edges[i];
    }
    edges.resize(kept);
}

void Model::finish() {
    compactEdges();
    edges.shrink_to_fit();
    out.build(nodes.size(), edges.size(), [&](size_t e) { return edges[e].from; });
    in.build(nodes.size(), edges.size(), [&](size_t e) { return edges[e].to; });
    // One control listed twice through exact copies of one net (`posedge clka or negedge rst_n or negedge r`, `wire r = rst_n`) is one
    // receiver. On the same edge it is one control with its known forced value; on opposite edges its polarity and forced value are
    // undecided: the first listing is kept with an unknown force.
    for (auto& f : flops) {
        if (f.resets.size() < 2 || f.resetValues.size() != f.resets.size() || f.resetKinds.size() != f.resets.size()) continue;
        const bool hasConditional = f.resetConditional.size() == f.resets.size();
        for (size_t j = 1; j < f.resets.size(); j++) {
            const NodeId root = aliasRoot(f.resets[j].signal);
            size_t first = j;
            for (size_t i = 0; i < j && first == j; i++)
                if (aliasRoot(f.resets[i].signal) == root) first = i;
            if (first == j) continue;
            if (f.resets[first].activeLow != f.resets[j].activeLow) {
                f.resetValues[first] = '?';
                f.resetKinds[first] = 2;
            }
            f.resetTransitionKnown = false;
            if (hasConditional) f.resetConditional[first] = f.resetConditional[first] || f.resetConditional[j];
            f.resets.erase(f.resets.begin() + static_cast<std::ptrdiff_t>(j));
            f.resetValues.erase(f.resetValues.begin() + static_cast<std::ptrdiff_t>(j));
            f.resetKinds.erase(f.resetKinds.begin() + static_cast<std::ptrdiff_t>(j));
            if (hasConditional) f.resetConditional.erase(f.resetConditional.begin() + static_cast<std::ptrdiff_t>(j));
            j--;
        }
    }
    // Propagate named constant-0/all-ones continuous drivers into constantProcs
    // so expressions like `(s2 ^ s3) & zer` (where `wire zer = 1'b0;` or `en ^ en`)
    // fold regardless of declaration order, while respecting competing drivers.
    if (!pairOperatorFacts.empty() && (!constDrivers.empty() || !foldedConstDrivers.empty() || !modeConstants.empty())) {
        std::unordered_map<const ValueSymbol*, std::vector<NodeId>> symNodes;
        for (NodeId n = 0; n < nodes.size(); ++n) {
            if (nodes[n].sym) symNodes[nodes[n].sym].push_back(n);
        }
        std::unordered_set<uint32_t> candidateProcs;
        for (const auto& fact : pairOperatorFacts)
            if (fact.rhs && fact.anchor && !constantProcs.count(fact.proc))
                candidateProcs.insert(fact.proc);
        std::unordered_map<uint32_t, std::vector<NodeId>> procTargets;
        if (!candidateProcs.empty()) {
            for (const auto& e : edges) {
                if (e.type == EdgeType::Comb && candidateProcs.count(e.proc))
                    procTargets[e.proc].push_back(e.to);
            }
            for (auto& [p, vec] : procTargets) {
                std::sort(vec.begin(), vec.end());
                vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
            }
            for (int iter = 0; iter < 8; ++iter) {
                bool changed = false;
                for (const auto& fact : pairOperatorFacts) {
                    if (!fact.rhs || !fact.anchor || constantProcs.count(fact.proc)) continue;
                    EvalContext ec(*fact.anchor);
                    const auto cf = Builder::foldConstantExpr(*fact.rhs, ec, false, this, &symNodes);
                    if (cf == Builder::ConstFold::None) continue;
                    constantProcs.insert(fact.proc);
                    changed = true;
                    if (cf == Builder::ConstFold::Zero || cf == Builder::ConstFold::AllOnes ||
                        cf == Builder::ConstFold::OneBitTrue) {
                        const char bitVal = (cf == Builder::ConstFold::Zero) ? '0' : '1';
                        if (auto it = procTargets.find(fact.proc); it != procTargets.end()) {
                            if (cf == Builder::ConstFold::OneBitTrue && it->second.size() != 1) continue;
                            for (NodeId tn : it->second)
                                foldedConstDrivers[tn].push_back({bitVal, EdgeType::Comb, fact.proc});
                        }
                    }
                }
                if (!changed) break;
            }
        }
    }
    // Propagate provably constant-stuck registers through sequential writes and toggle feedback.
    {
        std::unordered_set<const ValueSymbol*> specialSeqWriteSyms;
        std::unordered_set<const ValueSymbol*> specialAliasWriteSyms;
        for (const auto& sw : systemWrites) {
            if (sw.target) {
                specialSeqWriteSyms.insert(sw.target);
                specialAliasWriteSyms.insert(sw.target);
            }
        }
        for (const auto& cw : copyBackWrites) {
            if (cw.target) {
                specialSeqWriteSyms.insert(cw.target);
                specialAliasWriteSyms.insert(cw.target);
            }
        }
        for (const auto& pw : proceduralWrites) {
            const bool seqSpecial = pw.called || pw.fromCall || pw.forkOwnership || !pw.nonBlocking || !pw.value ||
                (pw.process && pw.process->procedureKind != slang::ast::ProceduralBlockKind::Always &&
                 pw.process->procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF);
            const bool aliasSpecial = pw.called || pw.fromCall || pw.forkOwnership || !pw.value ||
                (pw.process && (pw.process->procedureKind == slang::ast::ProceduralBlockKind::Initial ||
                                pw.process->procedureKind == slang::ast::ProceduralBlockKind::Final ||
                                pw.process->procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch));
            if (seqSpecial) {
                if (pw.target) specialSeqWriteSyms.insert(pw.target);
                for (const auto* mt : pw.mayWrite) if (mt) specialSeqWriteSyms.insert(mt);
            }
            if (aliasSpecial) {
                if (pw.target) specialAliasWriteSyms.insert(pw.target);
                for (const auto* mt : pw.mayWrite) if (mt) specialAliasWriteSyms.insert(mt);
            }
        }
        for (const auto& f : flops) {
            if (!nodes[f.q].sym || collapsed(f.q) || multiSequential.count(f.q) || f.blocking || f.hasIff ||
                !f.otherDomains.empty() || (f.q < blackBoxDriven.size() && blackBoxDriven[f.q]) ||
                (f.q < unresolvedDriven.size() && unresolvedDriven[f.q]) || uncertainCallDrivers.count(f.q) ||
                specialSeqWriteSyms.count(nodes[f.q].sym) || foldedConstDrivers.contains(f.q)) {
                stuckRejected.insert(f.q);
                continue;
            }
            bool hasComb = false;
            for (auto ei : in[f.q]) {
                if (edges[ei].type == EdgeType::Comb) { hasComb = true; break; }
            }
            if (!hasComb) {
                if (auto cit = constDrivers.find(f.q); cit != constDrivers.end()) {
                    for (const auto& cd : cit->second) {
                        if (cd.type == EdgeType::Comb) { hasComb = true; break; }
                    }
                }
            }
            if (hasComb) stuckRejected.insert(f.q);
        }
        std::unordered_map<NodeId, char> stuckUncond;
        auto safeAliasRoot = [&](NodeId n) -> NodeId {
            for (int guard = 0; guard < 32; guard++) {
                if (n >= nodes.size()) return kNoNode;
                if (nodes[n].sym && specialAliasWriteSyms.count(nodes[n].sym)) return kNoNode;
                if (n >= in.size() || in[n].size() != 1) return n;
                if (combSelfRewritten.contains(n) || constDrivers.contains(n) || foldedConstDrivers.contains(n) ||
                    (n < blackBoxDriven.size() && blackBoxDriven[n]) ||
                    (n < unresolvedDriven.size() && unresolvedDriven[n]) ||
                    uncertainCallDrivers.count(n))
                    return n;
                const Edge& e = edges[in[n][0]];
                if (!(e.type == EdgeType::Comb && e.exact) || isDeadEdge(e) ||
                    e.ctrl || e.approximate || e.shared)
                    return n;
                n = e.from;
            }
            return n;
        };
        auto knownUncondConst = [&](NodeId n) -> char {
            if (n == kNoNode) return '?';
            const NodeId root = safeAliasRoot(n);
            if (root == kNoNode) return '?';
            const char c = constantValue(n);
            if (c == '0' || c == '1') return c;
            if (auto it = stuckUncond.find(n); it != stuckUncond.end()) return it->second;
            if (auto it = stuckUncond.find(root); it != stuckUncond.end()) return it->second;
            return '?';
        };
        auto knownGuardedConst = [&](NodeId n) -> char {
            const char u = knownUncondConst(n);
            if (u == '0' || u == '1') return u;
            const NodeId root = safeAliasRoot(n);
            if (root == kNoNode) return '?';
            if (auto it = stuckFlopValues.find(n); it != stuckFlopValues.end()) return it->second;
            if (auto it = stuckFlopValues.find(root); it != stuckFlopValues.end()) return it->second;
            return '?';
        };
        for (int iter = 0; iter < 16; ++iter) {
            bool changed = false;
            for (const auto& f : flops) {
                if (stuckUncond.count(f.q) || stuckRejected.count(f.q)) continue;
                char val = '?';
                bool conflict = false;
                bool hasUncondAnchor = false;
                for (char rv : f.resetValues) {
                    if (rv == 'n') continue;
                    if (rv != '0' && rv != '1') { conflict = true; break; }
                    if (val == '?') val = rv;
                    else if (val != rv) { conflict = true; break; }
                    hasUncondAnchor = true;
                }
                if (!conflict) {
                    if (auto initIt = stuckInitValues.find(f.q); initIt != stuckInitValues.end()) {
                        if (val == '?') val = initIt->second;
                        else if (val != initIt->second) conflict = true;
                    }
                }
                if (conflict) {
                    stuckRejected.insert(f.q);
                    stuckFlopValues.erase(f.q);
                    stuckUncond.erase(f.q);
                    continue;
                }
                auto it = stuckCandidates.find(f.q);
                if (it == stuckCandidates.end()) continue;
                bool pendingUncond = false;
                bool pendingGuarded = false;
                for (const auto& w : it->second) {
                    using Kind = StuckFlopWrite::Kind;
                    if (w.kind == Kind::Const) {
                        if (w.constVal != '0' && w.constVal != '1') { conflict = true; break; }
                        if (val == '?') val = w.constVal;
                        else if (val != w.constVal) { conflict = true; break; }
                        if (w.unconditional) hasUncondAnchor = true;
                    } else if (w.kind == Kind::Copy) {
                        if (w.other == f.q) continue;
                        const char vu = knownUncondConst(w.other);
                        if (vu == '0' || vu == '1') {
                            if (val == '?') val = vu;
                            else if (val != vu) { conflict = true; break; }
                            if (w.unconditional) hasUncondAnchor = true;
                        } else {
                            pendingUncond = true;
                            if (f.resets.empty()) {
                                const char vg = knownGuardedConst(w.other);
                                if (vg == '0' || vg == '1') {
                                    if (val == '?') val = vg;
                                    else if (val != vg) { conflict = true; break; }
                                } else {
                                    pendingGuarded = true;
                                }
                            } else {
                                pendingGuarded = true;
                            }
                        }
                    } else if (w.kind == Kind::SelfXor) {
                        const char vu = knownUncondConst(w.other);
                        if (vu == '0') continue;
                        if (vu == '1') { conflict = true; break; }
                        pendingUncond = true;
                        pendingGuarded = true;
                    } else if (w.kind == Kind::SelfOr) {
                        const char vu = knownUncondConst(w.other);
                        if (vu == '0') continue;
                        if (vu == '1') {
                            if (val == '?') val = '1';
                            else if (val != '1') { conflict = true; break; }
                            if (w.unconditional) hasUncondAnchor = true;
                            continue;
                        }
                        pendingUncond = true;
                        pendingGuarded = true;
                    } else if (w.kind == Kind::SelfAnd) {
                        const char vu = knownUncondConst(w.other);
                        if (vu == '1') continue;
                        if (vu == '0') {
                            if (val == '?') val = '0';
                            else if (val != '0') { conflict = true; break; }
                            if (w.unconditional) hasUncondAnchor = true;
                            continue;
                        }
                        pendingUncond = true;
                        pendingGuarded = true;
                    }
                }
                if (conflict) {
                    stuckRejected.insert(f.q);
                    stuckFlopValues.erase(f.q);
                    stuckUncond.erase(f.q);
                    continue;
                }
                if (!pendingUncond && hasUncondAnchor && (val == '0' || val == '1')) {
                    if (stuckUncond.emplace(f.q, val).second) changed = true;
                    if (stuckFlopValues.emplace(f.q, val).second) changed = true;
                } else if (!pendingGuarded && (val == '0' || val == '1')) {
                    if (stuckFlopValues.emplace(f.q, val).second) changed = true;
                }
            }
            if (!changed) break;
        }
        stuckCandidates.clear();
        stuckCandidates.rehash(0);
        stuckRejected.clear();
        stuckRejected.rehash(0);
        stuckInitValues.clear();
        stuckInitValues.rehash(0);
    }
    for (auto& [node, summary] : combQualifierDrivers) {
        bool multi = summary.multipleDrivers || summary.proc == UINT32_MAX;
        if (!multi) {
            for (auto edgeId : in[node]) {
                const auto& edge = edges[edgeId];
                if (edge.type == EdgeType::Comb && edge.proc != summary.proc) {
                    multi = true;
                    break;
                }
            }
        }
        if (!multi) {
            if (auto cdIt = constDrivers.find(node); cdIt != constDrivers.end()) {
                for (const auto& cd : cdIt->second) {
                    if (cd.type == EdgeType::Comb && cd.proc != summary.proc) {
                        multi = true;
                        break;
                    }
                }
            }
        }
        if (multi) {
            summary.supported = false;
            summary.multipleDrivers = true;
            summary.arms.clear();
        }
    }
    // Compare reset polarities after connections are available. Keep the
    // original guard nodes for control-edge matching and reset inventory.
    for (auto& f : flops) {
        if (f.syncResetFacts.size() < 2) continue;
        std::map<NodeId, std::pair<bool, char>> seenFacts;
        bool conflicting = false;
        for (const auto& fact : f.syncResetFacts) {
            NodeId node = fact.node;
            bool activeLow = fact.activeLow;
            bool reachedRoot = false;
            for (unsigned step = 0; step < 64; ++step) {
                const auto incoming = in[node];
                if (incoming.size() != 1 || constDrivers.contains(node) ||
                    (node < blackBoxDriven.size() && blackBoxDriven[node]) ||
                    (node < unresolvedDriven.size() && unresolvedDriven[node])) {
                    reachedRoot = true;
                    break;
                }
                const auto& edge = edges[incoming[0]];
                if (edge.type != EdgeType::Comb || edge.ctrl || edge.approximate ||
                    edge.shared || !(edge.exact || edge.flip)) {
                    reachedRoot = true;
                    break;
                }
                node = edge.from;
                activeLow = activeLow != edge.flip;
            }
            if (!reachedRoot) {
                node = fact.node;
                activeLow = fact.activeLow;
            }
            const auto value = std::pair{activeLow, fact.forcedValue};
            auto [it, inserted] = seenFacts.emplace(node, value);
            if (!inserted && it->second != value) {
                conflicting = true;
                break;
            }
        }
        if (conflicting) {
            f.hasSyncResetCandidate = false;
            f.syncResetFacts.clear();
            f.syncResetGuards.clear();
            f.syncReset = kNoNode;
            f.syncActiveLow = false;
        }
    }
    // resolve clock roots, domains, reset roots
    std::map<NodeId, uint32_t> domainOfRoot;
    for (auto& f : flops) {
        if (f.clockResolved) {
            f.clockRoot = traceRoot(f.clockSignal);
            auto it = domainOfRoot.find(f.clockRoot.node);
            if (it == domainOfRoot.end()) {
                it = domainOfRoot.emplace(f.clockRoot.node, static_cast<uint32_t>(domains.size())).first;
                domains.push_back({f.clockRoot.node, f.clockRoot.kind, nameOf(f.clockRoot.node)});
            }
            f.domain = it->second;
        }
        for (auto& r : f.resets) r.root = traceRoot(r.signal);
    }
    // clocks of library RAMs and further sequential blocks are clock domains too (one may be the only user of its clock)
    auto domainOf = [&](const std::vector<NodeId>& clk, bool ramClock = false) -> int64_t {
        if (clk.empty() || (ramClock && clk.size() != 1)) return -1;
        Root r = traceRoot(clk[0]);
        if (ramClock && (r.kind == RootKind::Undriven || r.kind == RootKind::Cycle ||
                         r.kind == RootKind::BlackBox || constantValue(clk[0]) != '?')) return -1;
        auto it = domainOfRoot.find(r.node);
        if (it == domainOfRoot.end()) {
            it = domainOfRoot.emplace(r.node, static_cast<uint32_t>(domains.size())).first;
            domains.push_back({r.node, r.kind, nameOf(r.node)});
        }
        return it->second;
    };
    for (auto& f : flops)
        for (auto clk : f.otherClockSignals) f.otherDomains.push_back(static_cast<uint32_t>(domainOf({clk})));
    for (auto& ram : rams) {
        ram.writeDomain = domainOf(ram.writeClk, true);
        ram.readDomain = domainOf(ram.readClk, true);
    }
}

NodeId Model::aliasRoot(NodeId n) const {
    for (int guard = 0; guard < 32; guard++) {
        if (n >= in.size() || in[n].size() != 1) return n;
        if (combSelfRewritten.contains(n) || constDrivers.contains(n) || foldedConstDrivers.contains(n) ||
            (n < blackBoxDriven.size() && blackBoxDriven[n]) ||
            (n < unresolvedDriven.size() && unresolvedDriven[n]) ||
            uncertainCallDrivers.count(n))
            return n;
        const Edge& e = edges[in[n][0]];
        if (!(e.type == EdgeType::Comb && e.exact) || isDeadEdge(e) ||
            e.ctrl || e.approximate || e.shared)
            return n;
        n = e.from;
    }
    return n;
}

char Model::constantValue(NodeId n) const {
    if (n == kNoNode) return '?';
    if (auto it = modeConstants.find(n); it != modeConstants.end()) return it->second;
    n = aliasRoot(n);
    if (auto it = modeConstants.find(n); it != modeConstants.end()) return it->second;
    if (in[n].empty()) {
        auto it = constDrivers.find(n);
        if (it == constDrivers.end() || it->second.size() != 1) return '?';
        const auto& d = it->second[0];
        // Procedural constants may be conditional or retain state; only an unconditional zero-delay always_comb
        // assignment is an exact constant like a continuous assignment.
        const auto& p = procs[d.proc];
        const bool combConst = p.kind == ProcKind::Comb && !p.delayed && !p.bodyTiming && d.unconditional;
        if (d.type != EdgeType::Comb || (p.kind != ProcKind::Continuous && p.kind != ProcKind::PortConnection && !combConst)) return '?';
        return d.value;
    }
    auto fit = foldedConstDrivers.find(n);
    if (fit != foldedConstDrivers.end() && fit->second.size() == 1) {
        const auto& fd = fit->second[0];
        if (fd.type != EdgeType::Comb || procs[fd.proc].kind != ProcKind::Continuous) return '?';
        if (auto cit = constDrivers.find(n); cit != constDrivers.end()) {
            for (const auto& cd : cit->second)
                if (cd.proc != fd.proc) return '?';
        }
        for (auto ei : in[n]) {
            const auto& e = edges[ei];
            if (e.proc != fd.proc) return '?';
        }
        return fd.value;
    }
    return '?';
}

const Flop* Model::flopBehind(NodeId n, uint32_t* idx) const {
    for (int guard = 0; guard < 64; guard++) {
        auto it = flopOfQ.find(n);
        if (it != flopOfQ.end()) { if (idx) *idx = it->second; return &flops[it->second]; }
        if (in[n].size() != 1) return nullptr;
        const Edge& e = edges[in[n][0]];
        if (!(e.type == EdgeType::Comb && e.exact)) return nullptr;
        n = e.from;
    }
    return nullptr;
}

namespace {
/// Lower-case tokens of an identifier: split at `_`, `.`, a lower-to-upper case change and digits (digits are dropped, `rst1` is `rst`).
std::vector<std::string> nameTokens(std::string_view name) {
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&] { if (!cur.empty()) words.push_back(cur); cur.clear(); };
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalpha(c)) {
            if (std::isupper(c) && i && std::islower(static_cast<unsigned char>(name[i - 1]))) flush();
            cur += static_cast<char>(std::tolower(c));
        } else {
            flush();
        }
    }
    flush();
    return words;
}
} // namespace

ResetName classifyResetName(std::string_view name) {
    using Verdict = ResetName::Verdict;
    const auto words = nameTokens(name);
    auto isLowWord = [](const std::string& w) { return w == "n" || w == "ni" || w == "b" || w == "l" || w == "low" || w == "bar"; };
    // Words that say the net carries a status or a request, not a reset (`rst_done`, `reset_pending`). Only these make a name data.
    static const std::set<std::string, std::less<>> statusWords{
        "done", "req", "request", "ack", "en", "enable", "cnt", "count", "pending", "busy", "valid", "state", "flag", "active", "status",
        "stat", "ready", "seen"};
    // Words that make the name doubtful when they stand before the reset word (`force_rst`, `en_rst_n`, `do_reset`): the net may be a
    // request to reset, or a reset that only carries such a qualifier. The statuses count, and so do words that may also name a reset.
    static const std::set<std::string, std::less<>> qualifierWords{
        "start", "trigger", "force", "issue", "pulse", "release", "do", "gen", "generate", "assert"};
    // Words that name the domain or the source of a reset (`rst_core_n`, `reset_ddr`, `rst_n_meta`, `sw_rst`, `rst_lc_n`); they do not
    // change what it is.
    static const std::set<std::string, std::less<>> domainWords{
        "core", "aon", "ddr", "sys", "meta", "sw", "hw", "main", "mem", "periph", "per", "cpu", "dbg", "jtag", "ext", "int", "glb", "global",
        "soc", "top", "bus", "io", "pwr", "usb", "pcie", "phy", "hard", "soft", "sync", "async", "synced", "i", "o", "a", "lc",
        "rx", "tx"};
    // A letter or short word glued before the reset word names its domain (`wrst_n`, `rrst_n`: async FIFO write and read resets,
    // `ndmreset`: the debug module's reset of everything but itself, `presetn`: APB).
    static const std::set<std::string, std::less<>> prefixes{"", "a", "s", "h", "n", "p", "g", "t", "w", "r", "ndm", "sys", "pwr", "sync", "async",
                                                             "po", "dbg", "hw", "sw", "jtag", "ext", "soft", "hard", "cpu", "core", "aon", "ddr",
                                                             "glb", "global", "main", "bus"};
    struct Hit { bool is = false; std::string pre, glued; };
    std::vector<Hit> hits(words.size());
    ResetName result;
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        Hit& hit = hits[i];
        for (const char* c : {"reset", "rst", "por"}) {
            const std::string_view k = c;
            for (size_t at = 0; at + k.size() <= w.size(); ++at) {
                if (w.compare(at, k.size(), k) != 0) continue;
                const std::string tail = w.substr(at + k.size());
                if (!tail.empty() && !isLowWord(tail)) continue;
                if (!prefixes.count(w.substr(0, at))) continue;
                if (k == "por" && !w.substr(0, at).empty()) continue;
                if (w.substr(0, at) == "p" && tail.empty()) {   // `preset` is a value, `presetn` and `preset_n` the APB reset
                    bool lowAfter = false;
                    for (size_t j = i + 1; j < words.size(); ++j) lowAfter = lowAfter || isLowWord(words[j]);
                    if (!lowAfter) continue;
                }
                hit = {true, w.substr(0, at), tail};
                break;
            }
            if (hit.is) break;
        }
        result.resetWord = result.resetWord || hit.is;
    }
    if (!result.resetWord) return result;   // no reset word: data
    // Every reset word is tried: the name is a reset when one of them is followed by nothing but polarity, sync, domain tags or another
    // reset word (`por_rst_n`, `rst_por_n`, `reset_reset_n`). A status word after a reset word makes the name data when no word is clean.
    bool statusSeen = false;
    for (size_t i = 0; i < words.size(); ++i) {
        if (!hits[i].is) continue;
        bool low = !hits[i].glued.empty() || hits[i].pre == "n" || (i && words[i - 1] == "n");
        for (size_t j = i + 1; j < words.size(); ++j) low = low || isLowWord(words[j]);
        bool clean = true, status = false;
        for (size_t j = i + 1; j < words.size(); ++j) {
            const std::string& t = words[j];
            if (statusWords.count(t)) {
                // A status word that is itself followed by a polarity word (`rst_active_n`, `rst_cnt_n`) is written like a level that asserts
                // something: it may be a reset, so it makes the name doubtful rather than data.
                bool polarityAfter = false;
                for (size_t k = j + 1; k < words.size(); ++k) polarityAfter = polarityAfter || isLowWord(words[k]);
                status = status || !polarityAfter;
                clean = false;
                continue;
            }
            if (hits[j].is || isLowWord(t) || domainWords.count(t)) continue;
            // A one-letter tag after a polarity word is part of the same convention (`rst_ni_q`); alone it can name a stage (`rst_q`).
            if (t.size() == 1 && low) continue;
            clean = false;
        }
        statusSeen = statusSeen || status;
        if (!clean) continue;
        result.activeLow = low;
        bool qualified = false;
        for (size_t j = 0; j < i; ++j) qualified = qualified || statusWords.count(words[j]) || qualifierWords.count(words[j]);
        result.verdict = qualified ? Verdict::Undecided : Verdict::Known;
        return result;
    }
    // No clean reset word. Polarity reads from the first one, as an undecided net reads as its name does.
    for (size_t i = 0; i < words.size(); ++i)
        if (hits[i].is) {
            bool low = !hits[i].glued.empty() || hits[i].pre == "n" || (i && words[i - 1] == "n");
            for (size_t j = i + 1; j < words.size(); ++j) low = low || isLowWord(words[j]);
            result.activeLow = low;
            break;
        }
    result.verdict = statusSeen ? Verdict::Data : Verdict::Undecided;
    return result;
}

int resetNameKind(std::string_view name) {
    const ResetName n = classifyResetName(name);
    return n.verdict != ResetName::Verdict::Known ? 0 : (n.activeLow ? 2 : 1);
}

namespace {
/// The top-level statements of a procedure, in order: begin/end blocks and lists flattened, empty statements and declarations left out.
void topLevelStatements(const Statement& st, std::vector<const Statement*>& out) {
    if (st.kind == StatementKind::Timed) topLevelStatements(st.as<TimedStatement>().stmt, out);
    else if (st.kind == StatementKind::Block) topLevelStatements(st.as<BlockStatement>().body, out);
    else if (st.kind == StatementKind::List) { for (auto* child : st.as<StatementList>().list) topLevelStatements(*child, out); }
    else if (st.kind != StatementKind::Empty && st.kind != StatementKind::VariableDeclaration) out.push_back(&st);
}
} // namespace


namespace {
void buildResetIndex(const Model& m) {
    if (m.resetIndexed_) return;
    m.resetIndexed_ = true;
    for (const auto& flop : m.flops)
        for (const auto& reset : flop.resets) {
            const uint8_t bit = reset.activeLow ? 2 : 1;
            for (NodeId n : {reset.signal, reset.root.node}) {
                if (n == kNoNode || n >= m.nodes.size()) continue;
                m.resetNodePolarity_[n] |= bit;
                if (m.nodes[n].sym) m.resetSymPolarity_[m.nodes[n].sym] |= bit;
            }
        }
    const ValueSymbol* last = nullptr;
    bool lastOneBit = false;
    for (NodeId n = 0; n < m.nodes.size(); ++n) {
        const ValueSymbol* sym = m.nodes[n].sym;
        if (!sym) continue;
        if (sym != last) { last = sym; lastOneBit = sym->getType().isIntegral() && sym->getType().getBitWidth() == 1; }
        if (lastOneBit) m.oneBitNodes_[sym].push_back(n);
    }
}

/// The identity of one node of a net (`n`; kNoNode when only the symbol is known). The net is followed back through plain copies and
/// single inversions (`.clr(!rst_ni)`, `assign rst = ~rst_ni`) to the net that is the reset, with the polarity flipped for each inversion.
ResetClass classifyOne(const Model& m, const ValueSymbol* sym, NodeId n) {
    uint8_t bits = 0;
    const ValueSymbol* rootSym = nullptr;
    bool inverted = false;
    auto flipped = [](uint8_t b) { return static_cast<uint8_t>(((b & 1) << 1) | ((b & 2) >> 1)); };
    if (n == kNoNode) {
        if (sym) if (auto it = m.resetSymPolarity_.find(sym); it != m.resetSymPolarity_.end()) bits |= it->second;
    } else {
        if (auto it = m.resetNodePolarity_.find(n); it != m.resetNodePolarity_.end()) bits |= it->second;
        NodeId root = n;
        for (int step = 0; step < 64; ++step) {
            const auto& ie = m.in[root];
            if (ie.size() != 1) break;
            const Edge& e = m.edges[ie[0]];
            if (e.type != EdgeType::Comb || !(e.exact || e.flip)) break;
            inverted = inverted != e.flip;
            root = e.from;
        }
        if (auto it = m.resetNodePolarity_.find(root); it != m.resetNodePolarity_.end()) bits |= inverted ? flipped(it->second) : it->second;
        if (root < m.nodes.size()) rootSym = m.nodes[root].sym;
    }
    ResetName byName = sym ? classifyResetName(sym->name) : ResetName{};
    if (!byName.resetWord && rootSym && rootSym != sym) {
        byName = classifyResetName(rootSym->name);
        byName.activeLow = byName.activeLow != inverted;
    }
    ResetClass result;
    if (bits) {
        result.reset = result.fromInventory = true;
        if (bits == 3) {
            // asynchronous reset of both polarities: the name may say which, otherwise only a procedure's own edge can
            result.polarityKnown = byName.verdict == ResetName::Verdict::Known;
            result.activeLow = result.polarityKnown && byName.activeLow;
        } else {
            result.activeLow = bits == 2;
        }
        return result;
    }
    // Nontransparent logic cannot preserve reset polarity, but a reset-bearing
    // fan-in also cannot justify calling the result ordinary data. Keep its
    // identity undecided rather than inferring a reset through a gate.
    bool resetBearingLogic = false;
    if (n != kNoNode && byName.verdict == ResetName::Verdict::Data) {
        std::vector<NodeId> pending{n};
        std::set<NodeId> visited;
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            if (!visited.insert(node).second) continue;
            if (visited.size() > 256) { resetBearingLogic = true; break; }
            if (node != n) {
                const auto* source = m.nodes[node].sym;
                const auto name = source ? classifyResetName(source->name) : ResetName{};
                if (m.resetNodePolarity_.count(node) || name.verdict != ResetName::Verdict::Data) {
                    resetBearingLogic = true;
                    break;
                }
            }
            for (const auto edge : m.in[node])
                if (m.edges[edge].type == EdgeType::Comb) pending.push_back(m.edges[edge].from);
        }
    }
    result.reset = byName.verdict == ResetName::Verdict::Known;
    result.undecided = byName.verdict == ResetName::Verdict::Undecided || resetBearingLogic;
    result.activeLow = (result.reset || result.undecided) && byName.activeLow;   // an undecided net reads as its name does
    return result;
}

/// A net judged for several nodes (instances of one module body) is KNOWN or DATA only when they all agree.
ResetClass classifyCore(const Model& m, const ValueSymbol* sym, const std::vector<NodeId>& nodes) {
    buildResetIndex(m);
    if (nodes.empty()) return classifyOne(m, sym, kNoNode);
    ResetClass first = classifyOne(m, sym, nodes[0]);
    for (size_t i = 1; i < nodes.size(); ++i) {
        const ResetClass c = classifyOne(m, sym, nodes[i]);
        if (c.reset != first.reset || c.undecided != first.undecided || (c.reset && (c.activeLow != first.activeLow || c.polarityKnown != first.polarityKnown))) {
            ResetClass disagree;
            disagree.undecided = true;
            return disagree;
        }
        first.fromInventory = first.fromInventory && c.fromInventory;
    }
    return first;
}
} // namespace

ResetClass classifyReset(const Model& m, const ValueSymbol& sym) {
    buildResetIndex(m);
    static const std::vector<NodeId> none;
    auto it = m.oneBitNodes_.find(&sym);
    return classifyCore(m, &sym, it == m.oneBitNodes_.end() ? none : it->second);
}

ResetClass classifyResetNode(const Model& m, NodeId n) {
    if (n == kNoNode || n >= m.nodes.size()) return {};
    buildResetIndex(m);
    return classifyOne(m, m.nodes[n].sym, n);
}

namespace {
/// A reset test: a one-bit signal alone, inverted, or compared with a constant. `high` is true when the test holds while the signal is 1.
struct ResetTest { const ValueSymbol* sym = nullptr; bool high = true; };
std::optional<ResetTest> resetTestOf(const Expression* e, slang::ast::EvalContext& ec) {
    auto strip = [](const Expression* x) {
        while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
        return x;
    };
    auto plain = [](const Expression* x) {
        return (x->kind == ExpressionKind::NamedValue || x->kind == ExpressionKind::HierarchicalValue) && x->type->isIntegral() &&
               x->type->getBitWidth() == 1 && x->getSymbolReference() && x->getSymbolReference()->as_if<ValueSymbol>();
    };
    e = strip(e);
    ResetTest t;
    if (e->kind == ExpressionKind::UnaryOp && (e->as<UnaryExpression>().op == UnaryOperator::LogicalNot ||
                                               e->as<UnaryExpression>().op == UnaryOperator::BitwiseNot)) {
        t.high = false;
        e = strip(&e->as<UnaryExpression>().operand());
    } else if (e->kind == ExpressionKind::BinaryOp) {
        const auto& b = e->as<BinaryExpression>();
        const bool eq = b.op == BinaryOperator::Equality || b.op == BinaryOperator::CaseEquality;
        const bool ne = b.op == BinaryOperator::Inequality || b.op == BinaryOperator::CaseInequality;
        if (!eq && !ne) return std::nullopt;
        const Expression* sig = strip(&b.left());
        const Expression* cst = strip(&b.right());
        if (!plain(sig)) std::swap(sig, cst);
        if (!plain(sig)) return std::nullopt;
        const slang::ConstantValue cv = cst->eval(ec);
        if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
        const auto value = cv.integer().as<uint64_t>();
        const bool one = value && *value == 1, zero = value && *value == 0;
        if (!one && !zero) return std::nullopt;
        t.high = eq ? one : zero;
        t.sym = sig->getSymbolReference()->as_if<ValueSymbol>();
        return t;
    }
    if (!plain(e)) return std::nullopt;
    t.sym = e->getSymbolReference()->as_if<ValueSymbol>();
    return t;
}

/// The operands of a disjunction (`a || b | c`), or the expression itself; `compound` says there was more than one. A conjunction or any
/// other operator yields the whole expression as one operand, which is then no reset test.
void disjuncts(const Expression* e, std::vector<const Expression*>& out) {
    while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
    if (e->kind == ExpressionKind::BinaryOp) {
        const auto& b = e->as<BinaryExpression>();
        if (b.op == BinaryOperator::LogicalOr || (b.op == BinaryOperator::BinaryOr && b.type->isIntegral() && b.type->getBitWidth() == 1)) {
            disjuncts(&b.left(), out);
            disjuncts(&b.right(), out);
            return;
        }
    }
    out.push_back(e);
}

/// The polarity the procedure's own event list gives a net: 1 posedge, 2 negedge, 0 none or both.
int ownEdge(const TimingControl& t, const ValueSymbol* sym) {
    if (t.kind == TimingControlKind::SignalEvent) {
        const auto& s = t.as<SignalEventControl>();
        const Expression* x = &s.expr;
        while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
        if (x->kind == ExpressionKind::NamedValue && x->getSymbolReference() == sym)
            return s.edge == EdgeKind::PosEdge ? 1 : s.edge == EdgeKind::NegEdge ? 2 : 0;
        return 0;
    }
    if (t.kind == TimingControlKind::EventList) {
        int seen = 0;
        for (auto* e : t.as<EventListControl>().events) {
            const int one = ownEdge(*e, sym);
            if (one && seen && one != seen) return 0;
            seen = seen ? seen : one;
        }
        return seen;
    }
    return 0;
}
int ownEdge(const ProceduralBlockSymbol& p, const ValueSymbol* sym) {
    const Statement* st = &p.getBody();
    while (st->kind == StatementKind::Block) st = &st->as<BlockStatement>().body;
    return st->kind == StatementKind::Timed ? ownEdge(st->as<TimedStatement>().timing, sym) : 0;
}
} // namespace

namespace {
/// Whether the arm loads data and nothing else: it holds at least one assignment, every assignment stores a value that is not a constant,
/// and no subroutine call can store anything the arm does not show. A reset arm stores constants, so a test of a net whose identity is
/// undecided that guards such an arm is an enable, not a reset question (`if (valid) addr_q <= addr_i;`).
bool armLoadsOnlyData(const Statement& arm, const ProceduralBlockSymbol& process) {
    struct Walk : ASTVisitor<Walk, VisitFlags::AllGood> {
        slang::ast::EvalContext ec;
        size_t assignments = 0;
        bool constant = false, call = false;
        explicit Walk(const ProceduralBlockSymbol& p) : ec(p) { ec.pushEmptyFrame(); }
        void handle(const AssignmentExpression& a) {
            ++assignments;
            if (!a.right().eval(ec).bad()) constant = true;
            visitDefault(a);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall()) call = true;
            visitDefault(c);
        }
    } walk(process);
    arm.visit(walk);
    return walk.assignments > 0 && !walk.constant && !walk.call;
}
} // namespace

std::optional<SyncReset> synchronousReset(const ProceduralBlockSymbol& p, const Model& m, std::vector<const ConditionalStatement*>* enables) {
    std::vector<const Statement*> top;
    topLevelStatements(p.getBody(), top);
    slang::ast::EvalContext ec(p);
    SyncReset result;
    for (const Statement* st : top) {
        if (st->kind != StatementKind::Conditional) continue;
        const auto& c = st->as<ConditionalStatement>();
        if (c.conditions.size() != 1 || c.conditions[0].pattern) continue;
        std::vector<const Expression*> operands;
        disjuncts(c.conditions[0].expr, operands);
        const bool compound = operands.size() > 1;
        std::optional<ResetBranch> known;
        bool enable = false;
        std::vector<ResetBranch> readings;   // the ways an undecided test can be a reset: one, or one per polarity
        for (const Expression* operand : operands) {
            auto test = resetTestOf(operand, ec);
            if (!test) continue;
            ResetClass cls = classifyReset(m, *test->sym);
            if (!cls.reset && !cls.undecided) continue;
            // The procedure's own edge on the net is the polarity it resets with; the inventory and the name come after. A net that is an
            // asynchronous reset of both polarities, tested here without an edge of its own, may be either: undecided, read both ways.
            bool eitherPolarity = false;
            if (cls.reset) {
                if (const int edge = ownEdge(p, test->sym)) { cls.activeLow = edge == 2; cls.polarityKnown = true; }
                else if (!cls.polarityKnown) { cls.reset = false; cls.undecided = true; eitherPolarity = true; }
            }
            for (const bool low : {cls.activeLow, !cls.activeLow}) {
                if (low != cls.activeLow && !eitherPolarity) break;
                const bool thenIsReset = test->high == !low;
                if (compound && !thenIsReset) continue;   // the `then` of `a || b` does not run whenever `a` is deasserted
                ResetBranch b{&c, thenIsReset ? &c.ifTrue : c.ifFalse, thenIsReset ? c.ifFalse : &c.ifTrue, test->sym, compound, !low};
                if (!b.reset) continue;                   // `if (rst_n) <data>;` has no reset branch
                if (!cls.reset && !compound && armLoadsOnlyData(*b.reset, p)) { enable = true; continue; }   // an enable that loads data
                if (cls.reset) { if (!known) known = b; }
                else if (eitherPolarity || readings.empty()) readings.push_back(b);
            }
        }
        if (known) result.known.push_back(*known);
        else result.undecided.insert(result.undecided.end(), readings.begin(), readings.end());
        if (enable && !known && readings.empty() && enables) enables->push_back(&c);
    }
    if (result.known.empty() && result.undecided.empty()) return std::nullopt;
    return result;
}

std::optional<bool> conditionWhileAsserted(const Expression& condition, const slang::ast::ProceduralBlockSymbol& process, const ResetBranch& branch) {
    if (!branch.signal) return std::nullopt;
    slang::ast::EvalContext ec(process);
    ec.pushEmptyFrame();
    *ec.createLocal(branch.signal) = slang::SVInt(static_cast<slang::bitwidth_t>(1), branch.assertedHigh ? 1u : 0u, false);
    const slang::ConstantValue v = condition.eval(ec);
    if (!v.isInteger() || v.integer().hasUnknown()) return std::nullopt;
    return static_cast<bool>(v.isTrue());
}

bool storesConstantWhileAsserted(const ProceduralWrite& write, const ResetBranch& branch) {
    if (!write.value || !write.process || !branch.signal) return false;
    slang::ast::EvalContext ec(*write.process);
    ec.pushEmptyFrame();
    *ec.createLocal(branch.signal) = slang::SVInt(static_cast<slang::bitwidth_t>(1), branch.assertedHigh ? 1u : 0u, false);
    const slang::ConstantValue v = write.value->eval(ec);
    return v.isInteger() && !v.integer().hasUnknown();
}

Reach reachWhileAsserted(const ProceduralWrite& write, const ResetBranch& branch) {
    if (!write.process || !branch.signal || !branch.reset) return Reach::Unknown;
    auto at = std::find(write.scopes.begin(), write.scopes.end(), branch.reset);
    if (at == write.scopes.end()) return Reach::Unknown;
    bool runs = !write.afterPossibleExit;
    for (auto it = at + 1; it != write.scopes.end(); ++it) {
        const Statement& scope = **it;
        if (scope.kind != StatementKind::Conditional) { runs = false; continue; }
        const auto& c = scope.as<ConditionalStatement>();
        const Statement* arm = it + 1 != write.scopes.end() ? *(it + 1) : nullptr;
        const bool inThen = arm == &c.ifTrue;
        if (c.conditions.size() != 1 || c.conditions[0].pattern || !arm || (!inThen && arm != c.ifFalse)) { runs = false; continue; }
        const auto truth = conditionWhileAsserted(*c.conditions[0].expr, *write.process, branch);
        if (!truth) { runs = false; continue; }
        if (*truth != inThen) return Reach::Skipped;   // the other arm runs while the net is asserted
        ++it;                                           // the arm itself
    }
    return runs ? Reach::Runs : Reach::Unknown;
}

const std::vector<const ProceduralWrite*>& Model::writesOf(const ProceduralBlockSymbol* process) const {
    static const std::vector<const ProceduralWrite*> none;
    if (writesIndexed_ != proceduralWrites.size()) {
        writesByProcess_.clear();
        for (const auto& write : proceduralWrites) writesByProcess_[write.process].push_back(&write);
        writesIndexed_ = proceduralWrites.size();
    }
    auto it = writesByProcess_.find(process);
    return it == writesByProcess_.end() ? none : it->second;
}

Model& getModel(Design& design) {
    ++design.modelRequests;
    if (!design.model) design.model = std::shared_ptr<Model>(buildModel(design).release());
    return *design.model;
}

std::unique_ptr<Model> buildModel(Design& design) {
    auto m = std::make_unique<Model>();
    if (!design.compilation) return m;
    Builder b(design, *m);
    // run() calls finish(); flags must be applied before finish() resolves roots
    // (so split: run builds, then flags, then finish)
    b.run();
    m->indexFlops();
    return m;
}

} // namespace sverity::model
