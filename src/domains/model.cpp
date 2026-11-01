#include "domains/model.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

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
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"

namespace sverity::model {

using namespace slang;
using namespace slang::ast;

namespace {

constexpr uint32_t kMaxBits = 16384;         // wider symbols are collapsed to one node (recorded fallback); exact bit moves stay linear
constexpr uint64_t kMaxEdgesPerAssign = 1u << 22;

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
    SourceLocation loc;
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
            walkInstance(*top);
        }
        // mark top-level input ports
        for (auto* top : root.topInstances) markTopInputs(*top);
        applyFlags();
        m.finish();
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
    std::vector<Path> pathsOf(const Expression& e, EvalContext& ec, bool skipSelectors) {
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

    void addEdge(NodeId from, NodeId to, EdgeType t, uint32_t proc, bool exact, bool ctrl = false) {
        if (from == to) {
            if (t == EdgeType::Seq && !exact) m.selfLogic.insert(from);
            if (t == EdgeType::Comb && !exact) m.selfComb.emplace(from, proc);
            return;
        }
        Edge e{from, to, t, exact, proc, ctrl};
        if (const OpMap* om = ctrl ? curCtrl : curData) {
            auto it = om->find(m.nodes[from].sym);
            if (it != om->end()) {
                e.op = it->second.op;
                e.inv = it->second.inv;
                e.branch = it->second.br;
            }
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
        if (rcount * tcount > kMaxEdgesPerAssign) {
            fallback("edge-explosion", "dependency set of " + std::to_string(rcount) + " x " + std::to_string(tcount) +
                     " bits exceeds the limit; edges truncated, targets are marked as having unresolved drivers", loc);
            for (auto& tv : tn)
                for (auto t : tv) pendingFlags.push_back({t, 4});
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
        curData = &a.dataOps;
        curCtrl = &a.ctrlOps;
        struct Reset { Builder& b; ~Reset() { b.curData = b.curCtrl = nullptr; } } reset{*this};
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
        if (type == EdgeType::Seq) {
            bool hasData = false;
            if (a.pairsValid) { for (auto& pr : a.pairs) hasData = hasData || (pr.second.sym != nullptr); }
            else hasData = !a.dataReads.empty();
            if (hasData) {
                std::vector<NodeId> g;
                for (auto& r : ctrl) for (auto n : nodesFor(ctx, r)) g.push_back(n);
                for (auto& t : targets) for (auto n : nodesFor(ctx, t)) m.guards[n].push_back(g);
            }
        }
    }

    // ---------------------------------------------------------------- bit lists
    /// Expands an expression into its ordered (MSB first) bit sources when it is a pure bit move:
    /// static selects of one symbol, concatenations, constants and zero-extension. Returns false otherwise.
    bool bitList(const Expression& e, EvalContext& ec, std::vector<BitRef>& out) {
        const Expression* x = &e;
        uint64_t want = x->type ? x->type->getBitstreamWidth() : 0;
        if (x->kind == ExpressionKind::Conversion && x->as<ConversionExpression>().isImplicit()) {
            const Expression& op = x->as<ConversionExpression>().operand();
            uint64_t ow = op.type ? op.type->getBitstreamWidth() : 0;
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
        };
        std::vector<Ctl> control;

        void addReads(const Expression& e, std::vector<Path>& dst) {
            for (auto& p : b.pathsOf(e, ec, false)) dst.push_back(p);
        }
        void pushControl(const Expression& e, Branch br = Branch::None) {
            control.emplace_back();
            addReads(e, control.back().paths);
            classify(e, Op::None, 0, control.back().ops);
            control.back().br = br;
        }

        void handle(const ConditionalStatement& s) {
            size_t depth = control.size();
            for (auto& c : s.conditions) pushControl(*c.expr, Branch::Taken);
            s.ifTrue.visit(*this);
            control.resize(depth);
            if (s.ifFalse) {
                for (auto& c : s.conditions) pushControl(*c.expr, Branch::NotTaken);
                s.ifFalse->visit(*this);
                control.resize(depth);
            }
        }
        void handle(const CaseStatement& s) {
            size_t depth = control.size();
            pushControl(s.expr);
            for (auto& [sym, oi] : control.back().ops) { oi.op = Op::Compare; oi.inv = 2; }   // the selector is compared with the items
            // An arm executes only if no earlier arm matched: it depends on its own and all earlier item expressions.
            for (auto& g : s.items) {
                size_t first = control.size();
                for (auto* e : g.expressions) pushControl(*e, Branch::Taken);
                g.stmt->visit(*this);
                for (size_t k = first; k < control.size(); k++) control[k].br = Branch::NotTaken;   // later arms run only if this one did not match
            }
            if (s.defaultCase) s.defaultCase->visit(*this);
            control.resize(depth);
        }
        void handle(const ForLoopStatement& s) {
            size_t depth = control.size();
            for (auto* i : s.initializers) i->visit(*this);
            if (s.stopExpr) pushControl(*s.stopExpr);
            s.body.visit(*this);
            for (auto* st : s.steps) st->visit(*this);
            control.resize(depth);
        }
        void handle(const WhileLoopStatement& s) {
            size_t depth = control.size();
            pushControl(s.cond);
            s.body.visit(*this);
            control.resize(depth);
        }
        void handle(const DoWhileLoopStatement& s) {
            size_t depth = control.size();
            pushControl(s.cond);
            s.body.visit(*this);
            control.resize(depth);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall()) {
                b.fallback("user-function-call",
                           "call to '" + std::string(c.getSubroutineName()) + "': the subroutine body's data "
                           "dependencies are not traced (only argument reads are)", c.sourceRange.start());
            }
            visitDefault(c);
        }
        void handle(const AssignmentExpression& a) {
            Assign as;
            as.loc = toLoc(*b.sm, a.sourceRange.start());
            as.nonBlocking = a.isNonBlocking();
            as.targets = b.pathsOf(a.left(), ec, true);
            // selector reads on the LHS (e.g. mem[addr] <= d) influence the write
            auto all = b.pathsOf(a.left(), ec, false);
            for (auto& p : all)
                if (!std::binary_search(as.targets.begin(), as.targets.end(), p)) as.ctrlReads.push_back(p);
            for (auto& p : b.pathsOf(a.right(), ec, false)) as.dataReads.push_back(p);
            classify(a.right(), Op::None, 0, as.dataOps);
            if (a.isCompound())
                for (auto& t : as.targets) { as.dataReads.push_back(t); noteOp(as.dataOps, t.sym, Op::Arith, 2); }
            for (auto& p : as.ctrlReads) noteOp(as.ctrlOps, p.sym, Op::Select, 2);   // selector reads on the left-hand side
            for (auto& c : control) {
                for (auto& p : c.paths) as.ctrlReads.push_back(p);
                for (auto& [sym, oi] : c.ops) noteOp(as.ctrlOps, sym, oi.op, oi.inv, c.br);
            }
            if (!a.isCompound()) {
                std::vector<BitRef> L, R;
                if (b.bitList(a.left(), ec, L) && b.bitList(a.right(), ec, R) && L.size() == R.size() && !L.empty()) {
                    as.pairsValid = true;
                    for (size_t i = 0; i < L.size(); i++) as.pairs.push_back({L[i], R[i]});
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

    struct Event { int edge; Path path; bool ok; slang::SourceLocation loc; std::vector<Path> iff; };

    void collectEvents(const TimingControl& t, EvalContext& ec, std::vector<Event>& out) {
        if (t.kind == TimingControlKind::SignalEvent) {
            auto& se = t.as<SignalEventControl>();
            Event e{0, {}, false, t.sourceRange.start(), {}};
            if (se.iffCondition) e.iff = pathsOf(*se.iffCondition, ec, false);
            e.edge = se.edge == EdgeKind::PosEdge ? 1 : se.edge == EdgeKind::NegEdge ? 2 : se.edge == EdgeKind::BothEdges ? 3 : 0;
            auto ps = pathsOf(se.expr, ec, false);
            if (ps.size() == 1) { e.path = ps[0]; e.ok = true; }
            out.push_back(e);
        } else if (t.kind == TimingControlKind::EventList) {
            for (auto* c : t.as<EventListControl>().events) collectEvents(*c, ec, out);
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
            collectEvents(first->as<TimedStatement>().timing, ec, events);
            for (auto& e : events) if (e.edge) sequential = true;
        }
        if (pk == ProceduralBlockKind::AlwaysFF) sequential = true;

        std::vector<Assign> assigns;
        StmtVisitor v(*this, ec, assigns, ctx);
        body.visit(v);

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
                for (auto& a : tmp) {
                    // constants contribute no data reads
                    for (auto& r : a.dataReads) if (!(r == cp[0])) allConst = false;
                }
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

        // --- `@(posedge clk iff cond)`: the qualifier is an enable, i.e. a control dependency of every target
        for (auto& e : events)
            for (auto& a : assigns)
                for (auto& p : e.iff) a.ctrlReads.push_back(p);

        // --- edges and flops
        std::set<NodeId> qs;
        std::set<const ValueSymbol*> blockingState;
        std::set<const ValueSymbol*> readSoFar;
        for (size_t ai = 0; ai < assigns.size(); ai++) {
            auto& a = assigns[ai];
            for (auto* v : {&a.dataReads, &a.ctrlReads})
                for (auto& r : *v) readSoFar.insert(r.sym);
            std::vector<Path> moduleTargets, localTargets;
            for (auto& t : a.targets) {
                bool modLevel = isModuleLevel(*t.sym);
                if (modLevel && !a.nonBlocking) {
                    // A module-level variable written with a blocking assignment: it is a register only if it
                    // keeps state (read before/while written, or written conditionally). An unconditional write that is
                    // not read earlier in the block is a combinational temporary (no flop).
                    bool conditional = !a.ctrlReads.empty();
                    bool readBefore = readSoFar.count(t.sym) > 0;
                    if (!conditional && !readBefore && !readByOther(t.sym, &pb)) modLevel = false;
                    else {
                        blockingState.insert(t.sym);
                        fallback("blocking-assignment-register",
                                 "'" + std::string(t.sym->name) + "' is assigned with a blocking assignment in a clocked "
                                 "block and keeps state; treated as a register but never as a synchroniser stage",
                                 pb.location);
                    }
                }
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
                fallback("multiple-sequential-drivers",
                         "register '" + m.nameOf(q) + "' is assigned by more than one sequential block; only the first "
                         "block's clock is used", pb.location);
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
            f.hasSyncResetCandidate = syncCand;
            f.syncActiveLow = syncLow;
            f.syncReset = syncSig;
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
                if (handleLibCell(e->as<InstanceSymbol>(), ctx)) continue;
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
                    if (handleLibCell(ci, ctx)) break;
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
        std::map<std::string, std::vector<NodeId>> conn;
        for (auto* pc : ci.getPortConnections()) {
            if (!pc || !pc->getExpression()) continue;
            std::string pn(pc->port.name);
            for (auto& p : pathsOf(*pc->getExpression(), ec, declaredOut(pn)))
                for (auto n : nodesFor(ctx, p)) conn[pn].push_back(n);
        }
        auto get = [&](const std::string& pn) -> const std::vector<NodeId>& {
            static const std::vector<NodeId> none;
            auto f = conn.find(pn);
            return f == conn.end() ? none : f->second;
        };
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
        if (as.pairsValid) {
            for (auto& [t, src] : as.pairs) {
                if (!src.sym || !t.sym) continue;
                addEdge(nodeForBit(srcCtx, src), nodeForBit(tgtCtx, t), EdgeType::Comb, proc, true);
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
    if (id == kNoNode || id >= nodes.size()) return 0;
    const Type& t = nodes[id].sym->getType();
    return t.isFixedSize() ? t.getBitstreamWidth() : 0;
}
bool Model::collapsed(NodeId id) const {
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

void Model::finish() {
    in.assign(nodes.size(), {});
    out.assign(nodes.size(), {});
    // dedupe identical edges
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return std::tie(a.from, a.to, a.type, a.proc) < std::tie(b.from, b.to, b.type, b.proc);
    });
    edges.erase(std::unique(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return a.from == b.from && a.to == b.to && a.type == b.type && a.proc == b.proc;
    }), edges.end());
    for (uint32_t i = 0; i < edges.size(); i++) {
        out[edges[i].from].push_back(i);
        in[edges[i].to].push_back(i);
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
    // clocks of library RAMs are clock domains too (a RAM may be the only user of its clock)
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
