#include "analysis/flow.h"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/Type.h"

namespace sverity::flow {

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;

namespace {

// The most for-loop steps one procedure's analysis unrolls, over all its loops (slang counts one per step expression of each
// iteration, nested loops weighing more, and restarts the count for each loop; Impl lowers the limit for each loop by what the
// earlier ones used). A loop that needs more is not unrolled: it is walked once with its counter unknown and the result is
// approximate. Measured: a 65000-iteration loop takes 0.2 s and 124 MB, 8000 iterations 0.04 s and 48 MB; the default 65535 is
// affordable alone but not across many loops.
constexpr uint32_t kUnrollBudget = 8192;

AnalysisOptions flowOptions() {
    AnalysisOptions o;
    o.maxLoopAnalysisSteps = kUnrollBudget;
    return o;
}

using DefSet = std::vector<uint32_t>;   // sorted; 0 = the value from before the procedure, d > 0 = access index d-1

struct Seg {
    uint64_t lo = 0, hi = 0;
    DefSet blk;   // definitions a later read can see (blocking writes only)
    DefSet all;   // definitions that make up the final value (blocking and nonblocking)
};

// Lattice element. A variable that is absent has not been written on any path leading here (one segment, entry value).
struct State {
    bool reachable = true;
    std::map<const ValueSymbol*, std::vector<Seg>> vars;
};

uint64_t widthOf(const ValueSymbol& s) {
    const Type& t = s.getType();
    uint64_t w = t.isFixedSize() ? t.getBitstreamWidth() : 1;
    return w ? w : 1;
}

bool tracked(const ValueSymbol& s) { return s.kind == SymbolKind::Variable || s.kind == SymbolKind::Net || s.kind == SymbolKind::FormalArgument; }

bool moduleLevel(const Symbol& s) {
    auto* sc = s.getParentScope();
    if (!sc) return false;
    auto k = sc->asSymbol().kind;
    return k == SymbolKind::InstanceBody || k == SymbolKind::GenerateBlock || k == SymbolKind::CompilationUnit ||
           k == SymbolKind::Package;
}

DefSet unite(const DefSet& a, const DefSet& b) {
    DefSet out;
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

std::vector<Seg> entrySegs(uint64_t w) { return {Seg{0, w - 1, {0}, {0}}}; }

// Union of two segment lists over the same symbol, aligned on the union of their boundaries.
std::vector<Seg> combine(const std::vector<Seg>& a, const std::vector<Seg>& b) {
    std::vector<Seg> out;
    size_t i = 0, j = 0;
    uint64_t pos = 0;
    uint64_t end = std::max(a.back().hi, b.back().hi);
    while (pos <= end) {
        while (a[i].hi < pos) i++;
        while (b[j].hi < pos) j++;
        uint64_t hi = std::min(a[i].hi, b[j].hi);
        out.push_back({pos, hi, unite(a[i].blk, b[j].blk), unite(a[i].all, b[j].all)});
        if (hi == end) break;
        pos = hi + 1;
    }
    // merge neighbours with identical sets
    std::vector<Seg> merged;
    for (auto& s : out) {
        if (!merged.empty() && merged.back().blk == s.blk && merged.back().all == s.all) merged.back().hi = s.hi;
        else merged.push_back(std::move(s));
    }
    return merged;
}

} // namespace

struct Flow::Impl : AbstractFlowAnalysis<Impl, State> {
    using Base = AbstractFlowAnalysis<Impl, State>;
    Flow& f;
    EvalContext& ec;
    GuardMap guardMap;
    GuardList emptyGuards;
    const GuardList* curGuards = &emptyGuards;
    const AssignmentExpression* curAsg = nullptr;
    const UnaryExpression* curStep = nullptr;
    uint32_t unrollUsed = 0;       // budget used by the outermost loops finished so far
    uint32_t loopPeak = 0;         // the most steps the outermost loop being walked has counted
    int loopDepth = 0;
    int walkedDepth = 0;           // loops being walked that are not followed through their iterations
    const Statement* rootStmt = nullptr;
    State endState;
    std::set<std::pair<uint32_t, uint32_t>> killSeen;
    // What a user subroutine can reach outside itself, through the bodies it calls: the symbols it names (read or written), or
    // unknown when it cannot be told (a task, which may wait; a hierarchical name; a class or virtual method; an import; a call
    // that loops back; an output, inout or ref argument; a static local, which keeps its value between calls).
    struct Touch {
        bool unknown = false;
        std::set<const Symbol*> symbols;
    };
    std::map<const SubroutineSymbol*, Touch> touches;
    std::set<const SubroutineSymbol*> touching;
    static bool localTo(const Symbol& s, const SubroutineSymbol& sub) {
        for (const Scope* scope = s.getParentScope(); scope; scope = scope->asSymbol().getParentScope())
            if (&scope->asSymbol() == &sub) return true;
        return false;
    }
    const Touch& touchedBy(const SubroutineSymbol& sub) {
        if (auto it = touches.find(&sub); it != touches.end()) return it->second;
        Touch result;
        if (touching.count(&sub)) { result.unknown = true; return touches[&sub] = result; }   // loops back
        touching.insert(&sub);
        if (sub.subroutineKind != SubroutineKind::Function || sub.isVirtual() ||
            sub.flags.has(MethodFlags::DPIImport | MethodFlags::InterfaceExtern) || sub.getBody().bad())
            result.unknown = true;
        for (const auto* arg : sub.getArguments())
            if (arg->direction != ArgumentDirection::In) result.unknown = true;   // an output, inout or ref actual is written through the call
        if (!result.unknown) {
            struct Scan : ASTVisitor<Scan, VisitFlags::AllGood> {
                Impl& impl;
                const SubroutineSymbol& sub;
                Touch& out;
                Scan(Impl& i, const SubroutineSymbol& s, Touch& t) : impl(i), sub(s), out(t) {}
                void handle(const NamedValueExpression& e) {
                    const Symbol& s = e.symbol;
                    if (s.kind == SymbolKind::Parameter || s.kind == SymbolKind::EnumValue || s.kind == SymbolKind::Specparam) return;
                    if (localTo(s, sub)) {
                        // A static local keeps its value between calls: the call is not a function of its arguments.
                        const auto* local = s.kind == SymbolKind::Variable ? s.as_if<VariableSymbol>() : nullptr;
                        if (local && local->lifetime == VariableLifetime::Static && &s != sub.returnValVar) out.unknown = true;
                        return;
                    }
                    out.symbols.insert(&s);
                }
                void handle(const HierarchicalValueExpression&) { out.unknown = true; }
                void handle(const DisableStatement&) { out.unknown = true; }
                void handle(const DisableForkStatement&) { out.unknown = true; }
                void handle(const NewClassExpression&) { out.unknown = true; }
                void handle(const ConcurrentAssertionStatement& s) {
                    if (s.assertionKind == AssertionKind::Expect) out.unknown = true;
                    visitDefault(s);
                }
                // A declaration initializer, a block local's or a loop variable's, is a read the visitor does not reach by itself.
                void handle(const VariableDeclStatement& declaration) {
                    if (const auto* init = declaration.symbol.getInitializer()) init->visit(*this);
                }
                void handle(const ForLoopStatement& loop) {
                    for (const auto* var : loop.loopVars)
                        if (const auto* init = var->getInitializer()) init->visit(*this);
                    visitDefault(loop);
                }
                void handle(const CallExpression& call) {
                    if (call.isSystemCall()) {
                        const auto& name = std::get<CallExpression::SystemCallInfo>(call.subroutine).subroutine->name;
                        if (name == "$finish" || name == "$stop" || name == "$fatal" || name == "$exit") out.unknown = true;
                    } else {
                        const auto* callee = call.thisClass() ? nullptr : std::get_if<const SubroutineSymbol*>(&call.subroutine);
                        if (!callee || !*callee) out.unknown = true;
                        else {
                            const Touch& inner = impl.touchedBy(**callee);
                            out.unknown = out.unknown || inner.unknown;
                            out.symbols.insert(inner.symbols.begin(), inner.symbols.end());
                        }
                    }
                    visitDefault(call);
                }
            } scan(*this, sub, result);
            sub.getBody().visit(scan);
        }
        touching.erase(&sub);
        return touches[&sub] = std::move(result);
    }
    std::map<const CallExpression*, State> callStates;   // the state just before each user call (joined when it is reached again)

    Impl(Flow& flow, const Symbol& owner, const Statement& body)
        : Base(owner, flowOptions(), nullptr), f(flow), ec(getEvalContext()), guardMap(body, getEvalContext()) {
        rootStmt = &body;
        curGuards = &emptyGuards;
        if (auto* g = guardMap.of(rootStmt)) curGuards = g;
    }

    // ---- lattice interface required by AbstractFlowAnalysis
    State topState() const { return State{}; }
    State unreachableState() const {
        State s;
        s.reachable = false;
        return s;
    }
    State copyState(const State& s) const { return s; }
    void joinState(State& a, const State& b) {
        if (!b.reachable) return;
        if (!a.reachable) {
            a = b;
            return;
        }
        for (auto& [sym, bs] : b.vars) {
            auto it = a.vars.find(sym);
            if (it == a.vars.end()) a.vars[sym] = combine(entrySegs(widthOf(*sym)), bs);
            else it->second = combine(it->second, bs);
        }
        for (auto& [sym, as] : a.vars)
            if (!b.vars.count(sym)) as = combine(as, entrySegs(widthOf(*sym)));
    }
    // Both paths execute (loop body that certainly runs, fork-join): what happens later wins, earlier writes stay reachable.
    void meetState(State& a, const State& b) {
        if (b.reachable) a = b;
    }

    // ---- recording
    std::vector<Seg>& segsFor(State& st, const ValueSymbol& s) {
        auto it = st.vars.find(&s);
        if (it == st.vars.end()) it = st.vars.emplace(&s, entrySegs(widthOf(s))).first;
        return it->second;
    }
    static void splitAt(std::vector<Seg>& segs, uint64_t pos) {
        for (size_t i = 0; i < segs.size(); i++) {
            if (segs[i].lo < pos && pos <= segs[i].hi) {
                Seg right = segs[i];
                right.lo = pos;
                segs[i].hi = pos - 1;
                segs.insert(segs.begin() + i + 1, std::move(right));
                return;
            }
        }
    }
    // A user call may read any live write (exposed: an unknown, reported as Unresolved by rules);
    // a timing control lets other processes observe them (used).
    void markExposed() {
        f.opaque = true;
        forEachDef([&](uint32_t d) { f.accesses[d - 1].exposed = true; });
    }
    // A call whose body (and the bodies it calls) names only these symbols outside itself: only a write to one of them can have
    // been read, and nothing else in the procedure is affected. A callee touching nothing outside itself (a function of its
    // arguments) leaves the procedure as it was.
    void markExposed(const std::set<const Symbol*>& touched) {
        if (touched.empty()) return;
        f.opaque = true;
        forEachDef([&](uint32_t d) {
            if (touched.count(f.accesses[d - 1].sym)) f.accesses[d - 1].exposed = true;
        });
    }
    void markOpaque() {
        f.opaque = f.timing = true;
        forEachDef([&](uint32_t d) { f.accesses[d - 1].observed = true; });
    }
    template <typename F>
    void forEachDef(F fn) {
        for (auto& [sym, segs] : getState().vars)
            for (auto& s : segs)
                for (auto* set : {&s.blk, &s.all})
                    for (auto d : *set)
                        if (d) fn(d);
    }

    Access makeAccess(const ValueSymbol& sym, Range r, bool write, bool exact) {
        Access a;
        a.sym = &sym;
        a.range = r;
        a.write = write;
        a.exact = exact;
        a.whole = exact && r.first == 0 && r.second == widthOf(sym) - 1;
        a.local = !moduleLevel(sym);
        a.asg = curAsg;
        a.step = curStep;
        a.guards = curGuards;
        a.inWalkedLoop = walkedDepth > 0;
        a.inLoop = guardMap.inLoop(*curGuards);   // unrolled iterations share statements, so guard comparison between them is not meaningful
        return a;
    }

    void noteRead(const ValueSymbol& sym, Range r, bool exact, slang::SourceLocation loc, const Expression* /*e*/) {
        auto& st = getState();
        if (!st.reachable || !tracked(sym)) return;
        Access a = makeAccess(sym, r, false, exact);
        a.loc = loc;
        auto& segs = segsFor(st, sym);
        bool maybe = false, only = true, hard = false;
        DefSet reach;
        for (auto& s : segs) {
            if (s.hi < r.first || s.lo > r.second) continue;
            bool hasEntry = !s.blk.empty() && s.blk.front() == 0;
            if (hasEntry) {
                bool uncertain = false;   // a write that may be what fills these bits: run-time indexed, or in a loop not followed
                for (auto d : s.blk)
                    if (d && (!f.accesses[d - 1].exact || f.accesses[d - 1].inWalkedLoop)) uncertain = true;
                hard = hard || !uncertain;
                a.entrySegs.push_back({{std::max(s.lo, r.first), std::min(s.hi, r.second)},
                                       uncertain ? Access::Entry::Unsure : s.blk.size() == 1 ? Access::Entry::Sole : Access::Entry::Some});
            }
            maybe = maybe || hasEntry;
            only = only && s.blk.size() == 1 && hasEntry;
            reach = unite(reach, s.blk);
        }
        a.entryMaybe = maybe;
        a.entryOnly = only;
        a.entryHard = hard && !a.inWalkedLoop;
        for (auto d : reach)
            if (d) {
                a.reaching.push_back(d - 1);
                f.accesses[d - 1].used = true;
            }
        f.accesses.push_back(std::move(a));
    }

    void noteWrite(const ValueSymbol& sym, Range r, bool exact, bool nonblocking, bool compound, slang::SourceLocation loc) {
        auto& st = getState();
        if (!st.reachable || !tracked(sym)) return;
        Access a = makeAccess(sym, r, true, exact);
        a.nonblocking = nonblocking;
        a.compound = compound;
        a.loc = loc;
        f.accesses.push_back(std::move(a));
        uint32_t id = static_cast<uint32_t>(f.accesses.size());   // def id = index + 1
        auto& segs = segsFor(st, sym);
        splitAt(segs, r.first);
        splitAt(segs, r.second + 1);
        bool kill = exact;   // a compound write reads the old value first (its own Read access comes before it), then replaces the bits it covers
        for (auto& s : segs) {
            if (s.hi < r.first || s.lo > r.second) continue;
            if (kill) {
                for (auto d : s.all) {
                    if (!d || d == id) continue;
                    const Access& v = f.accesses[d - 1];
                    // the victim is replaced for good only when the write covers all of it and is of the same kind: a blocking write
                    // before a nonblocking one is overwritten, a nonblocking update is applied after any later blocking write
                    if (v.exact && !v.compound && v.range.first >= r.first && v.range.second <= r.second && v.nonblocking == nonblocking &&
                        killSeen.insert({d - 1, id - 1}).second)
                        f.kills.push_back({d - 1, id - 1, false, v.observed, v.exposed});
                }
                DefSet keep;
                if (!nonblocking)
                    for (auto d : s.all)
                        if (d && f.accesses[d - 1].nonblocking) keep.push_back(d);   // pending nonblocking updates survive a blocking write
                keep.push_back(id);
                std::sort(keep.begin(), keep.end());
                s.all = keep;
                if (!nonblocking) s.blk = {id};
            }
            else {
                // IDs increase monotonically. Appending preserves order and avoids quadratic copying.
                s.all.push_back(id);
                if (!nonblocking) s.blk.push_back(id);
            }
        }
    }

    // ---- expressions
    // Index expressions inside a select chain are ordinary reads.
    void visitSelectors(const Expression& e) {
        switch (e.kind) {
            case ExpressionKind::ElementSelect: {
                auto& s = e.as<ElementSelectExpression>();
                visitSelectors(s.value());
                this->visit(s.selector());
                break;
            }
            case ExpressionKind::RangeSelect: {
                auto& s = e.as<RangeSelectExpression>();
                visitSelectors(s.value());
                this->visit(s.left());
                this->visit(s.right());
                break;
            }
            case ExpressionKind::MemberAccess:
                visitSelectors(e.as<MemberAccessExpression>().value());
                break;
            default:
                break;
        }
    }

    // `v[i]` on a packed vector declared [N-1:0] with an unsigned selector of w bits (the selector as written, no arithmetic): the
    // bits it can reach end at element 2^w - 1.
    void narrowByIndexWidth(const Expression& e, const ValueSymbol& sym, Range& r) {
        if (e.kind != ExpressionKind::ElementSelect) return;
        const auto& es = e.as<ElementSelectExpression>();
        if (es.value().kind != ExpressionKind::NamedValue || &es.value().as<NamedValueExpression>().symbol != &sym) return;
        const Type& vt = sym.getType();
        if (!vt.isIntegral() || vt.isScalar() || !vt.hasFixedRange() || vt.getFixedRange().lower() != 0 || !vt.getFixedRange().isDescending()) return;
        const Expression& sel = es.selector().unwrapImplicitConversions();
        if (!sel.type || !sel.type->isIntegral() || sel.type->isSigned() || sel.type->getBitWidth() > 16) return;
        const uint64_t elems = uint64_t{1} << sel.type->getBitWidth();
        const uint64_t width = es.type->getBitWidth();
        if (!width) return;
        r.second = std::min(r.second, elems * width - 1);
        r.first = std::min(r.first, r.second);
    }

    bool pathOf(const Expression& e, const ValueSymbol*& sym, Range& r, bool& exact) {
        ValuePath p(e, ec);
        if (p.empty()) return false;
        sym = p.rootSymbol();
        if (!sym) return false;
        uint64_t w = widthOf(*sym);
        if (p.lsp) {
            r = p.lspBounds;
            if (r.first > r.second) std::swap(r.first, r.second);
            r.second = std::min(r.second, w - 1);
            r.first = std::min(r.first, r.second);
            exact = p.isFullyStatic();
            if (!exact) narrowByIndexWidth(e, *sym, r);
        }
        else {
            r = {0, w - 1};
            exact = false;
        }
        return true;
    }

    template <typename T>
        requires(IsAnyOf<T, ElementSelectExpression, RangeSelectExpression, MemberAccessExpression>)
    void handle(const T& e) {
        visitSelectors(e);
        if (&e == headerArray) return;
        const ValueSymbol* sym;
        Range r;
        bool exact;
        if (pathOf(e, sym, r, exact)) noteRead(*sym, r, exact, e.sourceRange.start(), &e);
        else this->visitExpr(e);
    }
    void handle(const NamedValueExpression& e) {
        if (&e == headerArray) return;
        noteRead(e.symbol, {0, widthOf(e.symbol) - 1}, true, e.sourceRange.start(), &e);
    }
    void handle(const HierarchicalValueExpression& e) {
        if (&e == headerArray) return;
        noteRead(e.symbol, {0, widthOf(e.symbol) - 1}, true, e.sourceRange.start(), &e);
    }

    void writeTarget(const Expression& e, bool nonblocking, bool compound) {
        switch (e.kind) {
            case ExpressionKind::Concatenation:
                for (auto* op : e.as<ConcatenationExpression>().operands()) writeTarget(*op, nonblocking, compound);
                return;
            case ExpressionKind::Conversion:
                writeTarget(e.as<ConversionExpression>().operand(), nonblocking, compound);
                return;
            case ExpressionKind::SimpleAssignmentPattern:   // `T'{a, b} <= x`: each element, wrapped by slang as an output-argument assignment,
                for (auto* op : e.as<SimpleAssignmentPatternExpression>().elements()) {   // takes the kind of the enclosing write
                    const auto* w = op->as_if<AssignmentExpression>();
                    writeTarget(w && w->isLValueArg() ? w->left() : *op, nonblocking, compound);
                }
                return;
            case ExpressionKind::NamedValue:
            case ExpressionKind::HierarchicalValue:
            case ExpressionKind::ElementSelect:
            case ExpressionKind::RangeSelect:
            case ExpressionKind::MemberAccess: {
                visitSelectors(e);
                const ValueSymbol* sym;
                Range r;
                bool exact;
                if (pathOf(e, sym, r, exact)) noteWrite(*sym, r, exact, nonblocking, compound, e.sourceRange.start());
                return;
            }
            default:
                this->visit(e);   // not a variable target (for example a streaming or pattern lvalue): treat its parts as reads
        }
    }

    void handle(const AssignmentExpression& a) {
        if (a.isLValueArg()) {
            writeTarget(a.left(), false, false);
            return;
        }
        if (a.timingControl) markOpaque();
        auto* saved = curAsg;
        curAsg = &a;
        this->visit(a.right());
        if (a.isCompound()) this->visit(a.left());
        curAsg = &a;
        writeTarget(a.left(), a.isNonBlocking(), a.isCompound());
        curAsg = saved;
    }

    void handle(const UnaryExpression& e) {
        switch (e.op) {
            case UnaryOperator::Preincrement:
            case UnaryOperator::Predecrement:
            case UnaryOperator::Postincrement:
            case UnaryOperator::Postdecrement: {
                auto* saved = curStep;
                curStep = &e;
                this->visit(e.operand());
                writeTarget(e.operand(), false, true);
                curStep = saved;
                break;
            }
            default:
                this->visitExpr(e);
        }
    }

    void handle(const CallExpression& c) {
        if (c.isSystemCall()) {
            const auto& name = std::get<CallExpression::SystemCallInfo>(c.subroutine).subroutine->name;
            if (name == "$finish" || name == "$stop" || name == "$fatal" || name == "$exit") markOpaque();
        } else {
            const State& here = getState();
            auto [it, fresh] = callStates.try_emplace(&c, here);
            if (!fresh) joinState(it->second, here);
            const auto* callee = c.thisClass() ? nullptr : std::get_if<const SubroutineSymbol*>(&c.subroutine);
            const Touch* touch = callee && *callee ? &touchedBy(**callee) : nullptr;
            if (!touch || touch->unknown) markExposed();
            else markExposed(touch->symbols);
        }
        this->visitExpr(c);
    }

    void handle(const NewClassExpression& e) {
        markExposed();
        this->visitExpr(e);
    }

    // ---- `for` loops: slang unrolls a loop only when its counter is declared by the header (or an automatic variable) and its bounds
    // are constant. A counter declared outside (a module-level `integer i`, a function-local `integer j`, which is static) is refused
    // for fear another process changes it, which cannot happen inside one activation: nothing else runs, and a call is the only way
    // the body could write it. So such a counter is made automatic while the base class walks the loop, when no assignment, step or
    // call in the body can write it. Every other loop that is not unrolled is walked once and marks the result approximate.
    struct CounterScan : ASTVisitor<CounterScan, VisitFlags::AllGood> {
        std::set<const Symbol*> vars;
        bool writes = false, calls = false, callsName = false;
        static const Symbol* rootOf(const Expression& e) {
            const Expression* x = &e;
            while (true) {
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
        void target(const Expression& e) {
            if (e.kind == ExpressionKind::Concatenation) {
                for (auto* op : e.as<ConcatenationExpression>().operands()) target(*op);
                return;
            }
            const Symbol* r = rootOf(e);
            writes = writes || !r || vars.count(r);   // a target that cannot be traced to a variable is taken to write it
        }
        void handle(const AssignmentExpression& a) { target(a.left()); visitDefault(a); }
        void handle(const UnaryExpression& u) {
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
                u.op == UnaryOperator::Postdecrement) target(u.operand());
            visitDefault(u);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall()) {
                calls = true;
                c.visitSymbolReferences([&](const Expression&, const Symbol& s) { callsName = callsName || vars.count(&s); });
            }
            visitDefault(c);
        }
    };
    // The static counters the header initialises, when nothing in the loop can write them; empty otherwise.
    std::vector<VariableSymbol*> unrollableCounters(const ForLoopStatement& stmt) {
        std::vector<VariableSymbol*> out;
        if (!stmt.loopVars.empty() || !stmt.stopExpr || stmt.steps.empty() || stmt.initializers.empty()) return out;
        CounterScan scan;
        for (auto* init : stmt.initializers) {
            const auto* a = init->as_if<AssignmentExpression>();
            const auto* n = a && !a->isCompound() ? a->left().as_if<NamedValueExpression>() : nullptr;
            const auto* v = n ? n->symbol.as_if<VariableSymbol>() : nullptr;
            if (!v || !v->getType().isIntegral()) return {};
            if (v->lifetime == VariableLifetime::Static) out.push_back(const_cast<VariableSymbol*>(v));
            scan.vars.insert(v);
        }
        stmt.body.visit(scan);
        bool moduleState = false;
        for (auto* v : out) moduleState = moduleState || moduleLevel(*v);
        if (scan.writes || scan.callsName || (moduleState && scan.calls)) return {};
        return out;
    }
    void visitStmt(const ForLoopStatement& stmt) {
        std::vector<VariableSymbol*> counters = unrollableCounters(stmt);
        for (auto* v : counters) v->lifetime = VariableLifetime::Automatic;
        struct Restore { std::vector<VariableSymbol*>& v; ~Restore() { for (auto* x : v) x->lifetime = VariableLifetime::Static; } } restore{counters};
        bool unrolled = false, dead = false, noSteps = false;
        if (stmt.stopExpr) {
            if (auto cv = tryEvalBool(*stmt.stopExpr)) dead = !cv.isTrue();
            else {   // what the base class will do: it unrolls exactly when this yields iteration values
                const uint32_t steps = forLoopSteps;
                SmallVector<ConstantValue> values;
                ForLoopVars vars;
                const auto will = tryGetLoopIterValues(stmt, values, vars);
                unrolled = !values.empty();
                dead = will == WillExecute::No && !unrolled;
                for (auto& [_, var] : vars) evalContext.deleteLocal(var);
                forLoopSteps = steps;
                if (dead) {
                    // The base class answers "No" both for a loop whose first test is false and for one whose first test it cannot
                    // evaluate (a run-time bound), and then takes the body for unreachable. Ask again with no steps, which it
                    // answers "Maybe" for the second case; that loop is walked once.
                    ForLoopStatement probe(stmt.initializers, stmt.stopExpr, {}, stmt.body, stmt.sourceRange);
                    probe.loopVars = stmt.loopVars;
                    SmallVector<ConstantValue> none;
                    ForLoopVars probeVars;
                    dead = tryGetLoopIterValues(probe, none, probeVars) == WillExecute::No;
                    for (auto& [_, var] : probeVars) evalContext.deleteLocal(var);
                    forLoopSteps = steps;
                    noSteps = !dead;
                }
            }
        }
        if (!unrolled && !dead) f.approximated = f.truncated = f.walkedOnce = true;
        struct Walked { int& d; bool on; Walked(int& depth, bool o) : d(depth), on(o) { d += on; } ~Walked() { d -= on; } } walkedScope(walkedDepth, !unrolled && !dead);
        if (noSteps) {
            ForLoopStatement walk(stmt.initializers, stmt.stopExpr, {}, stmt.body, stmt.sourceRange);
            walk.loopVars = stmt.loopVars;
            Base::visitStmt(walk);
        }
        else Base::visitStmt(stmt);
    }
    // ---- `foreach` over fixed-size dimensions is followed through its iterations, as slang's driver analysis does: each iteration binds
    // the loop variables, so a select on one is exact. The array named in the header supplies only its dimensions: its value is not
    // read. The base class walks a body once and joins its end state with the loop's breaks, so a body that can leave the loop early
    // (break, continue, return, disable) would apply the later iterations' writes to the paths that left: such a loop, a dynamic
    // dimension and a loop over the budget are walked once with the variables unknown (`walkedOnce`).
    const Expression* headerArray = nullptr;   // the array of the foreach being walked: named, not read
    struct JumpFinder : ASTVisitor<JumpFinder, VisitFlags::AllGood> {
        bool found = false;
        void handle(const BreakStatement&) { found = true; }
        void handle(const ContinueStatement&) { found = true; }
        void handle(const ReturnStatement&) { found = true; }
        void handle(const DisableStatement&) { found = true; }
        void handle(const DisableForkStatement&) { found = true; }
    };
    void visitStmt(const ForeachLoopStatement& stmt) {
        const Expression* outerHeader = std::exchange(headerArray, &stmt.arrayRef);
        std::vector<const ForeachLoopStatement::LoopDim*> dims;
        uint64_t total = 1;
        bool followed = true;
        for (const auto& d : stmt.loopDims) {
            if (!d.loopVar) continue;
            if (!d.range) { followed = false; break; }
            dims.push_back(&d);
            total *= d.range->width();
            if (total > options.maxLoopAnalysisSteps) { followed = false; break; }
        }
        if (dims.empty()) followed = false;
        if (followed) {
            JumpFinder jumps;
            stmt.body.visit(jumps);
            if (jumps.found) followed = false;
        }
        const uint32_t oldSteps = forLoopSteps;
        if (followed && oldSteps + total > options.maxLoopAnalysisSteps) {
            followed = false;
            f.approximated = f.truncated = true;
        }
        if (!followed) {
            f.walkedOnce = true;
            ++walkedDepth;
            Base::visitStmt(stmt);
            --walkedDepth;
            headerArray = outerHeader;
            return;
        }
        forLoopSteps = oldSteps + static_cast<uint32_t>(total);
        const bool savedUnrolled = std::exchange(inUnrolledForLoop, true);
        auto iterate = [&](auto&& self, size_t k) -> void {
            if (k == dims.size()) { Base::visitStmt(stmt); return; }
            const auto& dim = *dims[k];
            const Type& t = dim.loopVar->getType();
            ConstantValue* local = evalContext.createLocal(dim.loopVar, ConstantValue());
            const int32_t step = dim.range->left <= dim.range->right ? 1 : -1;
            for (int32_t i = dim.range->left;; i += step) {
                *local = SVInt(static_cast<bitwidth_t>(t.getBitWidth()), static_cast<uint64_t>(static_cast<int64_t>(i)), t.isSigned());
                self(self, k + 1);
                if (i == dim.range->right) break;
            }
            evalContext.deleteLocal(dim.loopVar);
        };
        iterate(iterate, 0);
        inUnrolledForLoop = savedUnrolled;
        forLoopSteps = oldSteps;
        headerArray = outerHeader;
    }
    using Base::visitStmt;

    // ---- statements: attach the path guards, flag timing, record declaration initialisers
    template <typename T>
        requires(std::is_base_of_v<Statement, T> && !std::is_same_v<T, Statement>)
    void handle(const T& s) {
        auto* saved = curGuards;
        if (auto* g = guardMap.of(&s)) curGuards = g;
        if constexpr (IsAnyOf<T, TimedStatement, WaitStatement, WaitOrderStatement, WaitForkStatement>) {
            if (&s != rootStmt) markOpaque();
        }
        if constexpr (std::is_same_v<T, ConcurrentAssertionStatement>) {
            if (s.assertionKind == AssertionKind::Expect) markOpaque();
        }
        // The unroll budget is per procedure: the outermost loop gets what the earlier ones left, and its use is the most steps slang
        // counted while walking it (nested loops included, as slang weighs them).
        struct Loop {
            Impl& i;
            const bool isLoop, outer;
            Loop(Impl& impl, bool loop) : i(impl), isLoop(loop), outer(loop && impl.loopDepth == 0) {
                if (isLoop) i.loopDepth++;
                if (outer) {
                    i.options.maxLoopAnalysisSteps = i.unrollUsed >= kUnrollBudget ? 0 : kUnrollBudget - i.unrollUsed;
                    i.loopPeak = 0;
                }
            }
            ~Loop() {
                if (isLoop) i.loopDepth--;
                if (outer) i.unrollUsed = std::min<uint32_t>(kUnrollBudget, i.unrollUsed + i.loopPeak);
            }
        } loop(*this, std::is_same_v<T, ForLoopStatement> || std::is_same_v<T, ForeachLoopStatement>);
        if (loopDepth > 0) loopPeak = std::max(loopPeak, forLoopSteps);
        // A loop whose unrolling stopped at its limit is visited once with the counter unknown: absence of a write, a latch or an
        // overwrite after it is not proven.
        if (forLoopSteps > options.maxLoopAnalysisSteps) f.approximated = f.truncated = f.walkedOnce = f.overBudget = true;
        if constexpr (IsAnyOf<T, WhileLoopStatement, DoWhileLoopStatement, RepeatLoopStatement, ForeverLoopStatement>) f.walkedOnce = true;   // a foreach decides for itself
        if constexpr (std::is_same_v<T, ContinueStatement>) f.approximated = f.pathsDropped = true;   // slang's base analysis drops the continue path
        if constexpr (std::is_same_v<T, CaseStatement>) {
            // slang counts items that cannot match (x/z bits, values wider than the selector) towards full coverage
            if (s.condition == CaseStatementCondition::Normal && s.expr.type) {
                uint64_t sw = s.expr.type->getBitWidth();
                for (auto& item : s.items)
                    for (auto* e : item.expressions) {
                        ConstantValue cv = e->eval(ec);
                        if (cv.isInteger() && (cv.integer().hasUnknown() || cv.integer().getMinRepresentedBits() > sw)) f.approximated = f.pathsDropped = true;
                    }
            }
        }
        constexpr bool walked = IsAnyOf<T, WhileLoopStatement, DoWhileLoopStatement, RepeatLoopStatement, ForeverLoopStatement>;
        walkedDepth += walked;
        this->visitStmt(s);
        walkedDepth -= walked;
        if constexpr (std::is_same_v<T, VariableDeclStatement>) {
            if (s.symbol.lifetime != VariableLifetime::Static && s.symbol.getInitializer())
                noteWrite(s.symbol, {0, widthOf(s.symbol) - 1}, true, false, false, s.sourceRange.start());
        }
        curGuards = saved;
    }

    void finish() {
        endState = getState();
        for (auto& [sym, segs] : endState.vars)
            for (auto& s : segs)
                for (auto d : s.all)
                    if (d) f.accesses[d - 1].live = true;
        // a read before the killer that can see the victim: smallest index of such a read per victim
        std::map<uint32_t, uint32_t> firstRead;
        for (uint32_t i = 0; i < f.accesses.size(); i++)
            if (!f.accesses[i].write)
                for (auto d : f.accesses[i].reaching) firstRead.emplace(d, i);
        for (auto& k : f.kills) {
            auto it = firstRead.find(k.victim);
            k.readBefore = it != firstRead.end() && it->second < k.killer;
        }
    }
};

const Statement& bodyOf(const ProceduralBlockSymbol& p) {
    const Statement& b = p.getBody();
    return b.kind == StatementKind::Timed ? b.as<TimedStatement>().stmt : b;
}

Style styleOf(const ProceduralBlockSymbol& p) {
    switch (p.procedureKind) {
        case ProceduralBlockKind::AlwaysComb: return Style::Comb;
        case ProceduralBlockKind::AlwaysLatch: return Style::Latch;
        case ProceduralBlockKind::AlwaysFF: return Style::Seq;
        case ProceduralBlockKind::Always: break;
        default: return Style::Other;
    }
    const Statement& body = p.getBody();
    if (body.kind != StatementKind::Timed) return Style::Other;
    const TimingControl& t = body.as<TimedStatement>().timing;
    if (t.kind == TimingControlKind::ImplicitEvent) return Style::Comb;
    std::vector<const TimingControl*> events;
    if (t.kind == TimingControlKind::EventList)
        events.assign(t.as<EventListControl>().events.begin(), t.as<EventListControl>().events.end());
    else
        events.push_back(&t);
    bool edge = false, other = events.empty();
    for (auto* e : events) {
        if (e->kind != TimingControlKind::SignalEvent) other = true;
        else if (e->as<SignalEventControl>().edge != EdgeKind::None) edge = true;
    }
    if (edge) return Style::Seq;
    return other ? Style::Other : Style::Level;
}

Flow::Flow(const ProceduralBlockSymbol& p) : impl_(std::make_unique<Impl>(*this, p, p.getBody())) {
    impl_->run(p.getBody());
    impl_->finish();
    bad = impl_->bad;
}

Flow::Flow(const SubroutineSymbol& routine) : impl_(std::make_unique<Impl>(*this, routine, routine.getBody())) {
    impl_->run(routine.getBody());
    impl_->finish();
    bad = impl_->bad;
}

Flow::~Flow() = default;

std::vector<Range> Flow::unassignedAtEnd(const ValueSymbol& sym) const {
    std::vector<Range> out;
    auto& st = impl_->endState;
    if (!st.reachable) return out;
    auto it = st.vars.find(&sym);
    if (it == st.vars.end()) return {{0, widthOf(sym) - 1}};
    for (auto& s : it->second)
        if (!s.all.empty() && s.all.front() == 0) {
            if (!out.empty() && out.back().second + 1 == s.lo) out.back().second = s.hi;
            else out.push_back({s.lo, s.hi});
        }
    return out;
}

std::vector<Range> Flow::entryRangesAtCall(const CallExpression& call, const ValueSymbol& sym) const {
    const std::vector<Range> whole{{0, widthOf(sym) - 1}};
    auto call_it = impl_->callStates.find(&call);
    if (call_it == impl_->callStates.end() || !call_it->second.reachable) return whole;
    auto it = call_it->second.vars.find(&sym);
    if (it == call_it->second.vars.end()) return whole;
    std::vector<Range> out;
    for (auto& s : it->second)
        if (!s.blk.empty() && s.blk.front() == 0) {
            if (!out.empty() && out.back().second + 1 == s.lo) out.back().second = s.hi;
            else out.push_back({s.lo, s.hi});
        }
    return out;
}

std::vector<const ValueSymbol*> Flow::writtenSymbols() const {
    std::vector<const ValueSymbol*> out;
    std::set<const ValueSymbol*> seen;
    for (auto& a : accesses)
        if (a.write && seen.insert(a.sym).second) out.push_back(a.sym);
    return out;
}

bool Flow::exclusive(const Access& a, const Access& b) const {
    std::vector<const Expression*> operands;
    if (!impl_->guardMap.exclusive(*a.guards, *b.guards, &operands)) return false;
    // the comparison proof holds only while its operand keeps its value from the start of the procedure until both accesses
    uint32_t hi = std::max(indexOf(a), indexOf(b));
    for (auto* op : operands) {
        ValuePath p(*op, impl_->ec);
        if (auto* sym = p.rootSymbol())
            for (uint32_t i = 0; i < hi && i < accesses.size(); i++)
                if (accesses[i].write && accesses[i].sym == sym) return false;
    }
    return true;
}

bool Flow::writtenBetween(const ValueSymbol& sym, uint32_t from, uint32_t to) const {
    for (uint32_t i = from + 1; i < to && i < accesses.size(); i++)
        if (accesses[i].write && accesses[i].sym == &sym) return true;
    return false;
}

} // namespace sverity::flow
