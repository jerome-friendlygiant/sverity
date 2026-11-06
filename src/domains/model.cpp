#include "domains/model.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>

#include "analysis/flow.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
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

namespace {

constexpr uint32_t kMaxBits = 16384;         // wider symbols are collapsed to one node (recorded fallback); exact bit moves stay linear
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
    std::vector<Path> targets;
    std::vector<Path> dataReads;   // RHS (and compound-assignment target) reads
    std::vector<Path> ctrlReads;   // control-flow conditions + selector reads
    OpMap dataOps, ctrlOps;        // operator context of each referenced variable (RHS / enclosing conditions)
    bool nonBlocking = false;
    bool pairsValid = false;       // dataReads are fully described by exact bit pairs below
    std::vector<std::pair<BitRef, BitRef>> pairs;   // (target bit, source bit or constant)
    bool approximate = false;      // a computed index or a user call: its bits are not known one by one
    bool bitDepsValid = false;     // not exact, but dataReads are described bit by bit below
    std::vector<std::pair<BitRef, std::vector<Path>>> bitDeps;   // (target bit, the reads that can influence that bit)
    std::vector<char> bitDepExact; // parallel to bitDeps: the bit is a plain move from one source bit
    SourceLocation loc;
    BoolFactPtr booleanValue;
    std::vector<GuardCondition> booleanGuards;
    std::vector<uint32_t> uncertainCalls;
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
        if (w > kMaxBits) {
            if (wideReported.insert(&s).second)
                fallback("wide-symbol", "'" + std::string(s.name) + "' is " + std::to_string(w) +
                         " bits wide; tracked as a single node (bit precision lost)", s.location);
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
    std::vector<Path> rawPathsOf(const Expression& e, EvalContext& ec, bool skipSelectors) {
        std::vector<Path> out;
        ValuePath::visitPaths(e, ec, [&](const ValuePath& vp) {
            const ValueSymbol* rs = vp.rootSymbol();
            if (!rs || !isGraphSymbol(*rs)) return;
            uint64_t w = widthOf(*rs);
            Path p{rs, 0, w ? w - 1 : 0};
            if (vp.lsp && w) { p.lo = vp.lspBounds.first; p.hi = vp.lspBounds.second; }
            if (p.lo > p.hi) std::swap(p.lo, p.hi);
            out.push_back(p);
        }, skipSelectors);
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
    struct SummaryVisitor : ASTVisitor<SummaryVisitor, VisitFlags::AllGood> {
        Builder& b;
        const SubroutineSymbol& sub;
        CallSummary& result;
        EvalContext ec;
        size_t visited = 0;
        bool sawAutomaticLocal = false;
        SummaryVisitor(Builder& builder, const SubroutineSymbol& routine, CallSummary& summary)
            : b(builder), sub(routine), result(summary), ec(routine) {}
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
                        // summary. A static local can carry state from an earlier call.
                        const auto* local = path.sym->as_if<VariableSymbol>();
                        if (!local || local->lifetime != VariableLifetime::Automatic)
                            result.unknown.insert("static subroutine-local state is not expanded");
                        else sawAutomaticLocal = true;
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
            if (declaration.symbol.lifetime != VariableLifetime::Automatic)
                result.unknown.insert("static subroutine-local state is not expanded");
            else sawAutomaticLocal = true;
        }
        void handle(const UnaryExpression& expr) {
            if (!tick()) return;
            if (expr.op == UnaryOperator::Preincrement || expr.op == UnaryOperator::Postincrement ||
                expr.op == UnaryOperator::Predecrement || expr.op == UnaryOperator::Postdecrement)
                paths(expr.operand(), true, true);
            visitDefault(expr);
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
            if (assignment.left().kind != ExpressionKind::NamedValue && assignment.left().kind != ExpressionKind::HierarchicalValue)
                assignment.left().visit(*this);
            assignment.right().visit(*this);
        }
        void handle(const CallExpression& call) {
            if (!tick()) return;
            this->visitDefault(call); // actual argument reads, including nested argument calls
            if (call.isSystemCall()) {
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
            result.unknown.insert("external or virtual subroutine has no resolved body"); result.unboundedWrites = true;
        } else {
            SummaryVisitor visitor(*this, sub, result);
            sub.getBody().visit(visitor);
            if (sub.subroutineKind == SubroutineKind::Function && !returnsValue(sub.getBody(), sub))
                result.unknown.insert("a return value is not established on every path");
            if (visitor.sawAutomaticLocal) checkLocalsAssigned(sub, result);
        }
        if (!result.writes.empty()) result.unknown.insert("writes to nonlocal state are not expanded as procedural effects");
        if (sub.hasOutputArgs()) result.unknown.insert("output, inout or ref argument effects are not expanded");
        activeCalls.erase(&sub);
        return result;
    }
    /// An automatic local holds no state, but a read before it is assigned on some path has no established value.
    void checkLocalsAssigned(const SubroutineSymbol& sub, CallSummary& result) {
        struct LoopIndices : ASTVisitor<LoopIndices, VisitFlags::AllGood> {
            std::set<const ValueSymbol*> found;
            void handle(const ForLoopStatement& loop) {
                for (const auto* variable : loop.loopVars) found.insert(variable);
                visitDefault(loop);
            }
        } indices;
        sub.getBody().visit(indices);
        flow::Flow flow(sub);
        if (flow.bad || flow.approximated) {
            result.unknown.insert("whether subroutine locals are assigned before use is not established");
            return;
        }
        for (const auto& access : flow.accesses) {
            if (access.write || !access.entryMaybe || !localTo(*access.sym, sub)) continue;
            if (access.sym->kind != SymbolKind::Variable || indices.found.count(access.sym)) continue;   // arguments, foreach iterators
            result.unknown.insert("a subroutine local can be read before it is assigned on some path");
            return;
        }
    }
    bool callPathResolved(const Path& path) const {
        const auto* body = bodyOf(*path.sym);
        if (!isModuleLevel(*path.sym) || !body) return false; // package state and nonlocal subroutine state need a state model
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
    const Expression& modeExpr(const Expression& e, EvalContext& ec) {
        if (!design.hasCaseMode()) return e;
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

    bool readsOwnWrite(NodeId n) const {
        if (!curEntryReads) return false;
        auto it = curEntryReads->find(m.nodes[n].sym);
        if (it == curEntryReads->end()) return false;
        if (it->second.empty()) return true;
        if (m.collapsed(n)) return false;   // the node stands for every bit
        uint64_t bit = m.nodes[n].bit;
        return std::none_of(it->second.begin(), it->second.end(), [&](const flow::Range& r) { return r.first <= bit && bit <= r.second; });
    }

    std::optional<EntryReads> entryReadsOf(const ProceduralBlockSymbol& pb, const std::vector<Assign>& assigns) {
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
        // A call is opaque to Flow, but its summary says what it can touch: one that writes nothing is an ordinary read of its
        // arguments (Flow records those) and of the state its body reads, which counts as a read of the entry value.
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
            if (!summary.unknown.empty() || summary.unboundedWrites || !summary.writes.empty() || call->thisClass()) return std::nullopt;
            const auto* routine = std::get_if<const SubroutineSymbol*>(&call->subroutine);
            if (!routine || (*routine)->subroutineKind != SubroutineKind::Function) return std::nullopt;
            for (const auto* formal : (*routine)->getArguments())
                if (formal->direction != ArgumentDirection::In) return std::nullopt;
            for (const auto& read : summary.reads) {
                if (!callPathResolved(read)) return std::nullopt;
                if (auto it = entry.find(read.sym); it != entry.end()) it->second.push_back({read.lo, read.hi});
            }
        }
        for (const auto& access : fl.accesses)
            if (!access.write && access.entryMaybe)
                if (auto it = entry.find(access.sym); it != entry.end()) it->second.push_back(access.range);
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
            Edge e;
            e.from = hub; e.to = target; e.proc = proc; e.type = type; e.ctrl = ctrl; e.approximate = curApproximate;
            e.op = Op::Other;
            e.inv = 2;
            pushEdge(e);
        }
    }

    void addEdge(NodeId from, NodeId to, EdgeType t, uint32_t proc, bool exact, bool ctrl = false) {
        if (from == to) {
            if (t == EdgeType::Seq && !ctrl) {
                if (exact) m.selfExact.insert(from);
                else m.selfLogic.insert(from);
            }
            if (t == EdgeType::Comb && !exact && !readsOwnWrite(from)) {
                if (m.selfComb.emplace(from, proc).second && curApproximate) m.selfCombApproximate.insert(from);
                else if (!curApproximate) m.selfCombApproximate.erase(from);
            }
            return;
        }
        Edge e;
        e.from = from; e.to = to; e.proc = proc; e.type = t; e.exact = exact; e.ctrl = ctrl;
        e.approximate = curApproximate;
        e.internalRead = t == EdgeType::Comb && readsOwnWrite(from);
        if (const OpMap* om = ctrl ? curCtrl : curData) {
            auto it = om->find(m.nodes[from].sym);
            if (it != om->end()) {
                e.op = it->second.op;
                e.inv = it->second.inv;
                e.branch = it->second.br;
            }
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
                    NodeId* /*unused*/ = nullptr) {
        if (!a.uncertainCalls.empty()) for (const auto& target : targets) for (auto node : nodesFor(ctx, target)) {
            auto& issues = m.uncertainCallDrivers[node];
            issues.insert(issues.end(), a.uncertainCalls.begin(), a.uncertainCalls.end());
        }
        curData = &a.dataOps;
        curCtrl = &a.ctrlOps;
        curApproximate = a.approximate;
        struct Reset { Builder& b; ~Reset() { b.curData = b.curCtrl = nullptr; b.curApproximate = false; } } reset{*this};
        if (a.pairsValid) {
            for (auto& [t, src] : a.pairs) {
                if (!src.sym && t.sym && src.value) {
                    bool sel = false;
                    for (auto& tp : targets) if (tp.sym == t.sym) sel = true;
                    if (sel) m.constDrivers[nodeForBit(ctx, t)].push_back({src.value, type, proc});
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
        } else if (a.bitDepsValid) {
            // Bits with one large read set (a leaf expression, a shared select) reach it through one hub.
            std::map<std::vector<Path>, std::vector<NodeId>> sharedReads;
            for (size_t index = 0; index < a.bitDeps.size(); index++) {
                const auto& [t, reads] = a.bitDeps[index];
                if (std::none_of(targets.begin(), targets.end(), [&](const Path& tp) { return tp.sym == t.sym; })) continue;
                const NodeId target = nodeForBit(ctx, t);
                if (reads.empty()) m.constDrivers[target].push_back({'?', type, proc});   // e.g. a literal padding bit
                uint64_t readBits = 0;
                for (const auto& read : reads) readBits += read.hi - read.lo + 1;
                if (readBits >= kHubReadSet) { sharedReads[reads].push_back(target); continue; }
                const bool exact = index < a.bitDepExact.size() && a.bitDepExact[index];
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
        }
        std::vector<Path> ctrl;
        for (auto& r : a.ctrlReads) {
            if (skipReads) {
                bool skip = false;
                for (auto n : nodesFor(ctx, r)) if (skipReads->count(n)) skip = true;
                if (skip) continue;
            }
            ctrl.push_back(r);
        }
        connect(ctx, ctrl, ctx, targets, type, proc, loc, true);
        if (type == EdgeType::Comb) {
            for (const auto& target : targets) for (auto node : nodesFor(ctx, target))
                m.booleanDrivers[node].push_back({a.booleanValue, a.booleanGuards.empty() && a.ctrlReads.empty()});
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
                for (const auto& target : targets) for (auto node : nodesFor(ctx, target))
                    m.qualifierGuards[node].push_back(conditions);
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

    bool hasDynamicSelect(const Expression& e, EvalContext& ec) {
        struct Selects : ASTVisitor<Selects, VisitFlags::AllGood> {
            EvalContext& context;
            bool found = false;
            explicit Selects(EvalContext& c) : context(c) {}
            void handle(const ElementSelectExpression& select) {
                EvalContext tmp(context.astCtx);
                found = found || select.selector().eval(tmp).bad();
                visitDefault(select);
            }
            void handle(const RangeSelectExpression& select) {
                EvalContext tmp(context.astCtx);
                found = found || select.left().eval(tmp).bad() || select.right().eval(tmp).bad();
                visitDefault(select);
            }
        } selects(ec);
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
            bool scalar = op.left().type->getBitstreamWidth() == 1 && op.right().type->getBitstreamWidth() == 1;
            if (op.op == BinaryOperator::LogicalAnd || (scalar && op.op == BinaryOperator::BinaryAnd))
                return boolFact(BoolFact::Kind::And, {booleanOf(op.left(), ec, ctx), booleanOf(op.right(), ec, ctx)});
            if (op.op == BinaryOperator::LogicalOr || (scalar && op.op == BinaryOperator::BinaryOr))
                return boolFact(BoolFact::Kind::Or, {booleanOf(op.left(), ec, ctx), booleanOf(op.right(), ec, ctx)});
        }
        return unknownBoolean(expr, ec, ctx);
    }

    // ---------------------------------------------------------------- statement walking
    struct StmtVisitor : ASTVisitor<StmtVisitor, VisitFlags::AllGood> {
        StmtVisitor(Builder& b, EvalContext& ec, std::vector<Assign>& out, const InstanceSymbol* ctx)
            : b(b), ec(ec), out(out), ctx(ctx) {}
        Builder& b;
        EvalContext& ec;
        std::vector<Assign>& out;
        const InstanceSymbol* ctx;
        struct Ctl {
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
            addReads(e, control.back().paths);
            classify(e, Op::None, 0, control.back().ops);
            control.back().br = br;
            control.back().uncertainCalls = b.callIssues(e, ctx);
            control.back().predicate = boolean ? b.booleanOf(e, ec, ctx) : b.unknownBoolean(e, ec, ctx);
        }

        // A constant-false conjunct cannot enable a branch, even when the
        // other operand is not constant. Do not invent a control edge for it.
        bool alwaysFalse(const Expression& expr) {
            // A call in the condition still runs, whatever the branch does, so its effects stay in the model.
            struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
                bool found = false;
                void handle(const CallExpression&) { found = true; }
            } calls;
            expr.visit(calls);
            if (calls.found) return false;
            if (b.design.hasCaseMode() && b.modeValue(expr, ec) == 0) return true;
            auto value = expr.eval(ec);
            if (!value.bad() && value.isInteger() && !value.hasUnknown()) return value.isFalse();
            if (expr.kind == ExpressionKind::Conversion)
                return alwaysFalse(expr.as<ConversionExpression>().operand());
            if (expr.kind == ExpressionKind::BinaryOp) {
                const auto& op = expr.as<BinaryExpression>();
                if (op.op == BinaryOperator::LogicalAnd)
                    return alwaysFalse(op.left()) || alwaysFalse(op.right());
            }
            return false;
        }
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
            if (s.conditions.size() == 1 && !s.conditions[0].pattern && alwaysFalse(*s.conditions[0].expr)) {
                if (s.ifFalse) s.ifFalse->visit(*this);
                return;
            }
            size_t depth = control.size();
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
        void handle(const ForLoopStatement& s) {
            size_t depth = control.size();
            for (const auto* variable : s.loopVars) b.m.loopVariables.insert(variable);
            for (auto* i : s.initializers) i->visit(*this);
            if (s.stopExpr) pushControl(*s.stopExpr, Branch::None, false);
            s.body.visit(*this);
            for (auto* st : s.steps) st->visit(*this);
            control.resize(depth);
        }
        void handle(const WhileLoopStatement& s) {
            size_t depth = control.size();
            pushControl(s.cond, Branch::None, false);
            s.body.visit(*this);
            control.resize(depth);
        }
        void handle(const DoWhileLoopStatement& s) {
            size_t depth = control.size();
            pushControl(s.cond, Branch::None, false);
            s.body.visit(*this);
            control.resize(depth);
        }
        void callEffects(const CallExpression& c) {
            if (!c.isSystemCall()) {
                const auto& summary = b.summaryFor(c);
                Assign effect;
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
                if (!effect.targets.empty()) {
                    // The write set is known, but procedural ordering / output
                    // argument transfer is not expanded. Preserve each affected
                    // state element as unresolved instead of inventing data edges.
                    effect.uncertainCalls = b.callIssues(c, ctx);
                    effect.loc = toLoc(*b.sm, c.sourceRange.start());
                    for (const auto& ctl : control) {
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
        void handle(const AssignmentExpression& a) {
            for (const auto* call : b.callsIn(a.left())) callEffects(*call);
            Assign as;
            as.loc = toLoc(*b.sm, a.sourceRange.start());
            as.nonBlocking = a.isNonBlocking();
            as.approximate = b.hasDynamicSelect(a.left(), ec) || b.hasDynamicSelect(a.right(), ec) || !b.callsIn(a.right()).empty();
            as.booleanValue = b.booleanOf(a.right(), ec, ctx);
            as.uncertainCalls = b.callIssues(a.right(), ctx);
            auto selectorCalls = b.callIssues(a.left(), ctx);
            as.uncertainCalls.insert(as.uncertainCalls.end(), selectorCalls.begin(), selectorCalls.end());
            as.targets = b.pathsOf(a.left(), ec, true);
            // selector reads on the LHS (e.g. mem[addr] <= d) influence the write
            // A constant selector reads nothing at run time, even if it names a signal inside $bits.
            std::vector<Path> all;
            if (as.approximate || b.hasDynamicSelect(a.left(), ec)) all = b.pathsOf(a.left(), ec, false);
            for (auto& p : all)
                if (!std::binary_search(as.targets.begin(), as.targets.end(), p)) as.ctrlReads.push_back(p);
            for (auto& p : b.pathsOf(a.right(), ec, false)) as.dataReads.push_back(p);
            classify(b.modeExpr(a.right(), ec), Op::None, 0, as.dataOps);
            if (a.isCompound())
                for (auto& t : as.targets) { as.dataReads.push_back(t); noteOp(as.dataOps, t.sym, Op::Arith, 2); }
            for (auto& p : as.ctrlReads) noteOp(as.ctrlOps, p.sym, Op::Select, 2);   // selector reads on the left-hand side
            if (!as.ctrlReads.empty()) {
                GuardCondition selector;
                selector.predicate = b.unknownBoolean(a.left(), ec, ctx);
                for (const auto& path : as.ctrlReads)
                    for (auto node : b.nodesFor(ctx, path)) selector.dependencies.push_back(node);
                as.booleanGuards.push_back(std::move(selector));
            }
            for (auto& c : control) {
                as.uncertainCalls.insert(as.uncertainCalls.end(), c.uncertainCalls.begin(), c.uncertainCalls.end());
                GuardCondition fact;
                fact.predicate = c.br == Branch::NotTaken ? b.boolFact(BoolFact::Kind::Not, {c.predicate}) : c.predicate;
                for (const auto& path : c.paths)
                    for (auto node : b.nodesFor(ctx, path)) fact.dependencies.push_back(node);
                as.booleanGuards.push_back(std::move(fact));
                for (auto& p : c.paths) as.ctrlReads.push_back(p);
                for (auto& [sym, oi] : c.ops) noteOp(as.ctrlOps, sym, oi.op, oi.inv, c.br);
            }
            if (!a.isCompound()) {
                std::vector<BitRef> L, R;
                if (b.bitList(a.left(), ec, L) && b.bitList(a.right(), ec, R) && L.size() == R.size() && !L.empty()) {
                    as.pairsValid = true;
                    for (size_t i = 0; i < L.size(); i++) as.pairs.push_back({L[i], R[i]});
                } else if (L.size() > 1 && std::all_of(L.begin(), L.end(), [](const BitRef& bit) { return bit.sym != nullptr; })) {
                    std::vector<std::vector<Path>> deps;
                    uint64_t budget = kMaxEdgesPerAssign;
                    if (b.bitwiseDeps(a.right(), ec, deps, budget) && deps.size() == L.size()) {
                        as.bitDepsValid = true;
                        as.bitDepExact = b.exactBitsOf(a.right(), ec, L.size());
                        for (size_t i = 0; i < L.size(); i++) as.bitDeps.push_back({L[i], std::move(deps[i])});
                    }
                }
            }
            for (auto* v : {&as.dataReads, &as.ctrlReads}) {
                std::sort(v->begin(), v->end());
                v->erase(std::unique(v->begin(), v->end()), v->end());
            }
            if (!as.targets.empty()) out.push_back(std::move(as));
            a.right().visit(*this);   // nested assignments / calls on the RHS
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

    struct Event { int edge; Path path; bool ok; slang::SourceLocation loc; std::vector<Path> iff; std::vector<uint32_t> uncertainCalls; };

    void collectEvents(const TimingControl& t, EvalContext& ec, std::vector<Event>& out, const InstanceSymbol* ctx) {
        if (t.kind == TimingControlKind::SignalEvent) {
            auto& se = t.as<SignalEventControl>();
            Event e{0, {}, false, t.sourceRange.start(), {}, {}};
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

    void handleProcedure(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx) {
        ProceduralBlockKind pk = pb.procedureKind;
        if (pk == ProceduralBlockKind::Initial || pk == ProceduralBlockKind::Final) return;
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
        StmtVisitor v(*this, ec, assigns, ctx);
        body.visit(v);

        // A latch keeps its pre-activation value by design, so its order proves nothing.
        std::optional<EntryReads> entryReads;
        if (pk != ProceduralBlockKind::AlwaysLatch) entryReads = entryReadsOf(pb, assigns);
        struct ResetEntry { Builder& b; ~ResetEntry() { b.curEntryReads = nullptr; } } resetEntry{*this};
        curEntryReads = entryReads ? &*entryReads : nullptr;

        ProcKind kind = sequential ? ProcKind::Sequential
                      : pk == ProceduralBlockKind::AlwaysLatch ? ProcKind::Latch : ProcKind::Comb;
        uint32_t proc = addProc(kind, pb.location, ctx);
        if (!sequential) {
            for (auto& a : assigns) emitAssign(ctx, a, a.targets, EdgeType::Comb, proc, pb.location);
            return;
        }
        m.numSeqProcedures++;
        analyseSequential(pb, ctx, ec, events, first, assigns, proc);
    }

    void analyseSequential(const ProceduralBlockSymbol& pb, const InstanceSymbol* ctx, EvalContext& ec,
                           const std::vector<Event>& events, const Statement* first, std::vector<Assign>& assigns,
                           uint32_t proc) {
        // --- identify clock and asynchronous resets among the edge events
        std::vector<const Event*> edgeEvents;
        for (auto& e : events) if (e.edge) edgeEvents.push_back(&e);
        const Statement* inner = first && first->kind == StatementKind::Timed ? firstMeaningful(&first->as<TimedStatement>().stmt) : nullptr;
        std::set<Path> topCond;
        const ConditionalStatement* topIf = nullptr;
        if (inner && inner->kind == StatementKind::Conditional) {
            topIf = &inner->as<ConditionalStatement>();
            // the whole if / else-if chain: `if (!rst_n) ... else if (!set_n) ...` tests two asynchronous controls
            for (const Statement* cur = topIf; cur && cur->kind == StatementKind::Conditional; ) {
                auto& cs = cur->as<ConditionalStatement>();
                for (auto& c : cs.conditions)
                    for (auto& p : pathsOf(*c.expr, ec, false)) topCond.insert(p);
                cur = cs.ifFalse ? firstMeaningful(cs.ifFalse) : nullptr;
            }
        }
        const Event* clk = nullptr;
        std::vector<const Event*> rsts;
        if (edgeEvents.size() == 1) {
            clk = edgeEvents[0];
        } else {
            std::vector<const Event*> nonReset;
            for (auto* e : edgeEvents) {
                if (e->ok && topCond.count(e->path)) rsts.push_back(e); else nonReset.push_back(e);
            }
            if (nonReset.size() == 1) clk = nonReset[0];
            else rsts.clear();   // ambiguous: cannot tell which is the clock
        }
        NodeId clockNode = kNoNode;
        int clockEdge = 0;
        bool clockOk = clk && clk->ok && edgeEvents.size() >= 1;
        if (clockOk) {
            auto ns = nodesFor(ctx, clk->path);
            clockNode = ns.empty() ? kNoNode : ns[0];
            clockEdge = clk->edge;
            if (clk->path.hi != clk->path.lo && widthOf(*clk->path.sym) > 1 && ns.size() != 1) clockOk = false;
        }
        if (!clockOk) {
            fallback("unresolved-clock", "could not identify a single clock event in this sequential block "
                     "(" + std::to_string(edgeEvents.size()) + " edge event(s))", pb.location);
        }
        std::vector<ResetInfo> resets;
        std::vector<const Event*> resetEvents;
        std::set<NodeId> resetNodes;
        for (auto* r : rsts) {
            auto ns = nodesFor(ctx, r->path);
            if (ns.empty()) continue;
            ResetInfo ri;
            ri.signal = ns[0];
            ri.async = true;
            ri.activeLow = r->edge == 2;
            ri.fromEdge = true;
            resets.push_back(ri);
            resetEvents.push_back(r);
            resetNodes.insert(ns[0]);
        }
        // value each asynchronous control forces on each register: constants assigned in the branch that tests that control
        std::vector<std::map<const ValueSymbol*, std::string>> controlKinds(resets.size());   // per reset: symbol -> value per bit (LSB first)
        struct ConstAssigns : ASTVisitor<ConstAssigns, VisitFlags::AllGood> {
            std::vector<const AssignmentExpression*> v;
            void handle(const AssignmentExpression& a) { v.push_back(&a); visitDefault(a); }
        };
        for (const Statement* cur = topIf; cur && cur->kind == StatementKind::Conditional;) {
            auto& c = cur->as<ConditionalStatement>();
            std::set<Path> condPaths;
            for (auto& cc : c.conditions) for (auto& p : pathsOf(*cc.expr, ec, false)) condPaths.insert(p);
            for (size_t ri = 0; ri < resetEvents.size(); ri++) {
                if (!condPaths.count(resetEvents[ri]->path)) continue;
                ConstAssigns ca;
                c.ifTrue.visit(ca);
                for (auto* a : ca.v) {
                    ValuePath vp(a->left(), ec);
                    auto* vs = vp.rootSymbol();
                    if (!vs || !isGraphSymbol(*vs)) continue;
                    uint64_t w = std::max<uint64_t>(widthOf(*vs), 1);
                    auto& bits = controlKinds[ri].try_emplace(vs, std::string(w, '\0')).first->second;   // '\0' = bit not assigned here
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
                    auto cv = a->right().eval(ec);
                    for (uint64_t b = lo; b <= hi; b++) {
                        char ch = '?';
                        if (stat && cv.isInteger() && b - lo < cv.integer().getBitWidth()) {
                            logic_t bit = cv.integer()[static_cast<int32_t>(b - lo)];
                            ch = bit.value == 0 ? '0' : bit.value == 1 ? '1' : bit.value == logic_t::z.value ? 'z' : 'x';
                        }
                        if (bits[b] == '\0') bits[b] = ch;
                        else if (bits[b] != ch) bits[b] = '?';
                    }
                }
            }
            cur = c.ifFalse ? firstMeaningful(c.ifFalse) : nullptr;
        }
        // heuristic synchronous reset candidate: top-level if on one signal, then-branch assigns constants
        bool syncCand = false, syncLow = false;
        NodeId syncSig = kNoNode;
        if (resets.empty() && topIf && topIf->conditions.size() == 1) {
            auto cp = pathsOf(*topIf->conditions[0].expr, ec, false);
            if (cp.size() == 1 && pathWidth(cp[0]) == 1) {
                std::vector<Assign> tmp;
                StmtVisitor tv(*this, ec, tmp, ctx);
                topIf->ifTrue.visit(tv);
                bool allConst = !tmp.empty();
                // Constants contribute no data reads. Reading the condition itself (`if (s) t <= s`) is data, not a reset value.
                for (auto& a : tmp) if (!a.dataReads.empty()) allConst = false;
                syncCand = allConst;
                if (syncCand) {
                    auto ns = nodesFor(ctx, cp[0]);
                    if (!ns.empty()) syncSig = ns[0];
                    const Expression* ce = topIf->conditions[0].expr;
                    while (ce->kind == ExpressionKind::Conversion) ce = &ce->as<ConversionExpression>().operand();
                    if (ce->kind == ExpressionKind::UnaryOp) {
                        auto op = ce->as<UnaryExpression>().op;
                        syncLow = op == UnaryOperator::LogicalNot || op == UnaryOperator::BitwiseNot;
                    }
                }
            }
        }

        // A final, else-free constant override is equivalent to a reset-first branch, but only
        // for bits with one unconditional exact nonblocking data write earlier in the block.
        // Keep the control edges: a reset driven from another domain is still a crossing.
        std::set<NodeId> trailingResetBits;
        NodeId trailingReset = kNoNode;
        bool trailingLow = false;
        bool unqualifiedClock = std::all_of(events.begin(), events.end(), [](const Event& e) { return e.iff.empty(); });
        if (resets.empty() && unqualifiedClock && first && first->kind == StatementKind::Timed) {
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
                const auto& last = list.back()->as<ConditionalStatement>();
                if (!last.ifFalse && last.conditions.size() == 1 && !last.conditions[0].pattern) {
                    const Expression* cond = last.conditions[0].expr;
                    while (cond->kind == ExpressionKind::Conversion) cond = &cond->as<ConversionExpression>().operand();
                    if (cond->kind == ExpressionKind::UnaryOp) {
                        const auto& unary = cond->as<UnaryExpression>();
                        if (unary.op == UnaryOperator::LogicalNot || unary.op == UnaryOperator::BitwiseNot) {
                            trailingLow = true;
                            cond = &unary.operand();
                            while (cond->kind == ExpressionKind::Conversion) cond = &cond->as<ConversionExpression>().operand();
                        }
                    }
                    auto cp = pathsOf(*last.conditions[0].expr, ec, false);
                    if (cond->kind == ExpressionKind::NamedValue && cp.size() == 1 && pathWidth(cp[0]) == 1) {
                        auto guards = nodesFor(ctx, cp[0]);
                        auto branch = statements(&last.ifTrue);
                        std::set<NodeId> overrides, exactData;
                        std::map<NodeId, NodeId> dataSource;
                        std::map<NodeId, unsigned> priorWrites;
                        bool valid = guards.size() == 1 && !branch.empty();
                        for (auto* stmt : branch) {
                            if (stmt->kind != StatementKind::ExpressionStatement) { valid = false; break; }
                            const auto& expr = stmt->as<ExpressionStatement>().expr;
                            if (expr.kind != ExpressionKind::Assignment) { valid = false; break; }
                            const auto& assignment = expr.as<AssignmentExpression>();
                            std::vector<Assign> a;
                            StmtVisitor visitor(*this, ec, a, ctx);
                            stmt->visit(visitor);
                            std::vector<BitRef> targets;
                            auto value = assignment.right().eval(ec);
                            if (!assignment.isNonBlocking() || assignment.isCompound() || !value.isInteger() ||
                                a.size() != 1 || !a[0].ctrlReads.empty() ||
                                !bitList(assignment.left(), ec, targets) || targets.empty()) { valid = false; break; }
                            for (const auto& target : targets) {
                                if (!target.sym) { valid = false; break; }
                                overrides.insert(nodeForBit(ctx, target));
                            }
                        }
                        for (size_t i = 0; valid && i + 1 < list.size(); ++i) {
                            std::vector<Assign> prior;
                            StmtVisitor visitor(*this, ec, prior, ctx);
                            list[i]->visit(visitor);
                            for (const auto& a : prior)
                                for (const auto& target : a.targets)
                                    for (auto bit : nodesFor(ctx, target)) ++priorWrites[bit];
                            if (list[i]->kind != StatementKind::ExpressionStatement || prior.size() != 1) continue;
                            const auto& a = prior[0];
                            if (!a.nonBlocking || !a.pairsValid || !a.ctrlReads.empty()) continue;
                            for (const auto& [target, source] : a.pairs)
                                if (target.sym && source.sym) {
                                    NodeId targetBit = nodeForBit(ctx, target);
                                    exactData.insert(targetBit);
                                    dataSource[targetBit] = nodeForBit(ctx, source);
                                }
                        }
                        if (valid) {
                            trailingReset = guards[0];
                            for (auto bit : overrides)
                                if (priorWrites[bit] == 1 && exactData.count(bit) && dataSource[bit] != trailingReset)
                                    trailingResetBits.insert(bit);
                        }
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
        for (const auto& e : events) if (!e.iff.empty() || !e.uncertainCalls.empty()) plainEvents = false;
        if (plainEvents && sole == topIf && resets.size() == 1 && topIf && topIf->ifFalse &&
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
        std::set<NodeId> qs;
        // Decide once per symbol: later conditional writes can make an earlier
        // blocking assignment stateful. Mixing Comb and Seq for the same symbol
        // would hide the first assignment from the register's fan-in cone.
        std::set<const ValueSymbol*> blockingState;
        std::set<const ValueSymbol*> readSoFar;
        for (const auto& a : assigns) {
            for (auto* v : {&a.dataReads, &a.ctrlReads})
                for (auto& r : *v) readSoFar.insert(r.sym);
            if (a.nonBlocking) continue;
            for (auto& t : a.targets)
                if (isModuleLevel(*t.sym) &&
                    (!a.uncertainCalls.empty() || !a.ctrlReads.empty() || readSoFar.count(t.sym) || readByOther(t.sym, &pb)))
                    blockingState.insert(t.sym);
        }
        for (auto* sym : blockingState)
            fallback("blocking-assignment-register",
                     "'" + std::string(sym->name) + "' is assigned with a blocking assignment in a clocked "
                     "block and keeps state; treated as a register but never as a synchroniser stage", pb.location);
        for (auto& a : assigns) {
            std::vector<Path> moduleTargets, localTargets;
            for (auto& t : a.targets) {
                bool modLevel = isModuleLevel(*t.sym);
                if (modLevel && !a.nonBlocking && !blockingState.count(t.sym)) modLevel = false;
                (modLevel ? moduleTargets : localTargets).push_back(t);
            }
            if (!localTargets.empty()) emitAssign(ctx, a, localTargets, EdgeType::Comb, proc, pb.location);
            if (moduleTargets.empty()) continue;
            emitAssign(ctx, a, moduleTargets, EdgeType::Seq, proc, pb.location, &resetNodes);
            std::vector<Path> asyncReads;
            for (auto* v : {&a.ctrlReads, &a.dataReads})
                for (auto& r : *v)
                    for (auto n : nodesFor(ctx, r))
                        if (resetNodes.count(n)) { asyncReads.push_back(r); break; }
            if (!asyncReads.empty()) connect(ctx, asyncReads, ctx, moduleTargets, EdgeType::AsyncCtl, proc, pb.location);
            for (auto& t : moduleTargets)
                for (auto n : nodesFor(ctx, t)) qs.insert(n);
        }
        for (NodeId q : qs) {
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
            f.resets = resets;
            for (size_t ri = 0; ri < resets.size(); ri++) {
                auto it = controlKinds[ri].find(m.nodes[q].sym);
                char ch = 'n';   // 'n': this reset branch does not assign the bit
                if (it != controlKinds[ri].end()) {
                    const std::string& bits = it->second;
                    uint32_t bit = m.nodes[q].bit;
                    if (m.collapsed(q)) {   // one node stands for the whole vector: a value only if every bit agrees
                        ch = bits.empty() ? 'n' : bits[0];
                        for (char c : bits) if (c != ch) ch = '?';
                    }
                    else if (bit < bits.size()) ch = bits[bit];
                    if (ch == '\0') ch = 'n';
                }
                f.resetValues.push_back(ch);
                f.resetKinds.push_back(ch == '0' ? 0 : ch == '1' ? 1 : 2);
            }
            if (auto it = transitions.find(q); it != transitions.end()) {
                f.resetTransitionKnown = true;
                f.resetValues[0] = it->second.asserted;
                f.resetKinds[0] = it->second.asserted == '1' ? 1 : 0;
                const auto& src = it->second.released;
                if (src.sym) f.releasedSource = nodeForBit(ctx, src);
                else f.releasedValue = src.value;
            }
            bool trailing = trailingResetBits.count(q) != 0;
            f.hasSyncResetCandidate = syncCand || trailing;
            f.syncActiveLow = trailing ? trailingLow : syncLow;
            f.syncReset = trailing ? trailingReset : syncSig;
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
        for (auto& a : as) emitAssign(ctx, a, a.targets, EdgeType::Comb, proc, loc);
    }

    // ---------------------------------------------------------------- hierarchy
    void collectInstances(const InstanceSymbol& inst) {
        bodyInstances[&inst.body].push_back(&inst);
        collectScope(inst.body);
    }
    void collectScope(const Scope& sc) {
        for (auto& mem : sc.members()) {
            switch (mem.kind) {
                case SymbolKind::Instance: collectInstances(mem.as<InstanceSymbol>()); break;
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
            if (e->kind == SymbolKind::Instance) collectInstances(e->as<InstanceSymbol>());
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
        for (auto& mem : scope.members()) {
            switch (mem.kind) {
                case SymbolKind::ProceduralBlock: handleProcedure(mem.as<ProceduralBlockSymbol>(), ctx); break;
                case SymbolKind::ContinuousAssign: {
                    auto& ca = mem.as<ContinuousAssignSymbol>();
                    handleAssignExpr(ca.getAssignment(), ca, ctx, ca.location);
                    break;
                }
                case SymbolKind::Net: {
                    auto& net = mem.as<NetSymbol>();
                    if (auto* init = net.getInitializer()) {
                        // `wire w = expr;` is a continuous assignment to w
                        EvalContext ec(net);
                        std::vector<Path> reads = pathsOf(*init, ec, false);
                        Path t{&net, 0, widthOf(net) ? widthOf(net) - 1 : 0};
                        uint32_t proc = addProc(ProcKind::Continuous, net.location, ctx);
                        Assign as;
                        as.targets = {t};
                        as.dataReads = reads;
                        classify(modeExpr(*init, ec), Op::None, 0, as.dataOps);
                        as.booleanValue = booleanOf(*init, ec, ctx);
                        as.uncertainCalls = callIssues(*init, ctx);
                        std::vector<BitRef> R;
                        uint64_t w = widthOf(net);
                        if (w && bitList(*init, ec, R) && R.size() == w) {
                            as.pairsValid = true;
                            for (uint64_t i = 0; i < w; i++) as.pairs.push_back({{&net, static_cast<uint32_t>(w - 1 - i)}, R[i]});
                        }
                        emitAssign(ctx, as, as.targets, EdgeType::Comb, proc, net.location);
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
                        if (!e->isUninstantiated) processScope(*e, ctx);
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
        if (kind == "and" || kind == "nand" || kind == "or" || kind == "nor" || kind == "xor" || kind == "xnor") {
            outs.insert(0);
            for (size_t i = 1; i < conns.size(); i++) ins.insert(i);
        } else if ((kind == "buf" || kind == "not") && !conns.empty()) {
            for (size_t i = 0; i + 1 < conns.size(); i++) outs.insert(i);
            ins.insert(conns.size() - 1);
        } else {
            fallback("primitive", "primitive '" + kind + "' is not modelled precisely; all connections are treated as "
                     "mutually dependent", pi.location);
            for (size_t i = 0; i < conns.size(); i++) { outs.insert(i); ins.insert(i); }
        }
        for (auto o : outs)
            for (auto i : ins) {
                if (o == i) continue;
                connect(ctx, paths[i], ctx, paths[o], EdgeType::Comb, proc, pi.location);
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
        for (auto* pc : ci.getPortConnections()) {
            if (!pc || !pc->getExpression()) continue;
            auto issues = callIssues(*pc->getExpression(), ctx);
            connectionIssues.insert(connectionIssues.end(), issues.begin(), issues.end());
            std::string pn(pc->port.name);
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
        else if (kind == "ram") { ok = true; for (auto& p : cell.ports) ok = ok && !get(p.name).empty(); }
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
                if (!src.sym && t.sym && src.value) m.constDrivers[nodeForBit(tgtCtx, t)].push_back({src.value, EdgeType::Comb, proc});
                if (!src.sym || !t.sym) continue;
                addEdge(nodeForBit(srcCtx, src), nodeForBit(tgtCtx, t), EdgeType::Comb, proc, true);
            }
        } else if (as.bitDepsValid) {
            for (const auto& [t, reads] : as.bitDeps) {
                const NodeId target = nodeForBit(tgtCtx, t);
                if (reads.empty()) m.constDrivers[target].push_back({'?', EdgeType::Comb, proc});
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
    std::vector<std::string_view> rel;
    const Scope* sc = n.sym->getParentScope();
    while (sc) {
        const Symbol& ss = sc->asSymbol();
        if (ss.kind == SymbolKind::InstanceBody) break;
        if (!ss.name.empty()) rel.push_back(ss.name);
        sc = ss.getParentScope();
    }
    for (auto it = rel.rbegin(); it != rel.rend(); ++it) { s += "."; s += std::string(*it); }
    s += "." + std::string(n.sym->name);
    uint64_t w = n.sym->getType().isFixedSize() ? n.sym->getType().getBitstreamWidth() : 0;
    if (w > 1 && w <= kMaxBits) s += "[" + std::to_string(n.bit) + "]";
    return s;
}

uint64_t Model::symbolWidth(NodeId id) const {
    if (id == kNoNode || id >= nodes.size() || !nodes[id].sym) return 0;
    const Type& t = nodes[id].sym->getType();
    return t.isFixedSize() ? t.getBitstreamWidth() : 0;
}
bool Model::collapsed(NodeId id) const {
    if (id != kNoNode && id < nodes.size() && !nodes[id].sym) return false;   // a logic hub stands for no signal
    uint64_t w = symbolWidth(id);
    return w == 0 || w > kMaxBits;
}

Root Model::traceRoot(NodeId start) const {
    NodeId cur = start;
    std::unordered_set<NodeId> seen;
    for (;;) {
        if (!seen.insert(cur).second) return {cur, RootKind::Cycle};
        if (cur < blackBoxDriven.size() && blackBoxDriven[cur]) return {cur, RootKind::BlackBox};
        const auto& ie = in[cur];
        if (ie.empty()) return {cur, cur < topInput.size() && topInput[cur] ? RootKind::TopPort : RootKind::Undriven};
        if (ie.size() == 1 && edges[ie[0]].type == EdgeType::Comb && edges[ie[0]].exact) { cur = edges[ie[0]].from; continue; }
        return {cur, RootKind::Logic};
    }
}

void Model::compactEdges() {
    // A signal can be both the data and the enable of one target, and both facts matter: `if (s) t <= s` holds t when s is low,
    // so it is no synchroniser stage. They stay separate edges. Among repeats of one role, inexact is kept before exact.
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        const auto key = [](const Edge& e) { return std::make_tuple(e.from, e.to, static_cast<int>(e.type), e.proc, e.ctrl, e.exact); };
        return key(a) < key(b);
    });
    // A repeat can differ in what it says, as when one condition guards the assignments on both of its branches. Keeping whichever
    // came first would depend on the input order, so the survivor says what is common to all of them, as noteOp does for one assignment.
    size_t kept = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (kept) {
            Edge& last = edges[kept - 1];
            const Edge& e = edges[i];
            if (last.from == e.from && last.to == e.to && last.type == e.type && last.proc == e.proc && last.ctrl == e.ctrl) {
                if (last.op != e.op) last.op = Op::Other;
                if (last.inv != e.inv) last.inv = 2;
                if (last.branch != e.branch) last.branch = Branch::None;
                last.approximate = last.approximate || e.approximate;
                last.internalRead = last.internalRead && e.internalRead;
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
    auto domainOf = [&](const std::vector<NodeId>& clk) -> int64_t {
        if (clk.empty()) return -1;
        Root r = traceRoot(clk[0]);
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
        ram.writeDomain = domainOf(ram.writeClk);
        ram.readDomain = domainOf(ram.readClk);
    }
}

NodeId Model::aliasRoot(NodeId n) const {
    for (int guard = 0; guard < 32; guard++) {
        if (in[n].size() != 1) return n;
        const Edge& e = edges[in[n][0]];
        if (!(e.type == EdgeType::Comb && e.exact)) return n;
        n = e.from;
    }
    return n;
}

char Model::constantValue(NodeId n) const {
    if (n == kNoNode) return '?';
    if (auto it = modeConstants.find(n); it != modeConstants.end()) return it->second;
    n = aliasRoot(n);
    if (auto it = modeConstants.find(n); it != modeConstants.end()) return it->second;
    if (!in[n].empty()) return '?';
    auto it = constDrivers.find(n);
    if (it == constDrivers.end() || it->second.size() != 1) return '?';
    const auto& d = it->second[0];
    // Procedural constants may be conditional or retain state.
    if (d.type != EdgeType::Comb || (procs[d.proc].kind != ProcKind::Continuous &&
                                   procs[d.proc].kind != ProcKind::PortConnection)) return '?';
    return d.value;
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

Model& getModel(Design& design) {
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
    return m;
}

} // namespace sverity::model
