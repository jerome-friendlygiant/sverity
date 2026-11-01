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

bool tracked(const ValueSymbol& s) { return s.kind == SymbolKind::Variable || s.kind == SymbolKind::Net; }

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
    const Statement* rootStmt = nullptr;
    State endState;
    std::set<std::pair<uint32_t, uint32_t>> killSeen;

    Impl(Flow& flow, const ProceduralBlockSymbol& p)
        : Base(p, AnalysisOptions{}, nullptr), f(flow), ec(getEvalContext()), guardMap(p.getBody(), getEvalContext()) {
        rootStmt = &p.getBody();
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
    void markOpaque() {
        f.opaque = true;
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
        a.guards = curGuards;
        a.inLoop = guardMap.inLoop(*curGuards);   // unrolled iterations share statements, so guard comparison between them is not meaningful
        return a;
    }

    void noteRead(const ValueSymbol& sym, Range r, bool exact, slang::SourceLocation loc, const Expression* /*e*/) {
        auto& st = getState();
        if (!st.reachable || !tracked(sym)) return;
        Access a = makeAccess(sym, r, false, exact);
        a.loc = loc;
        auto& segs = segsFor(st, sym);
        bool maybe = false, only = true;
        DefSet reach;
        for (auto& s : segs) {
            if (s.hi < r.first || s.lo > r.second) continue;
            bool hasEntry = !s.blk.empty() && s.blk.front() == 0;
            maybe = maybe || hasEntry;
            only = only && s.blk.size() == 1 && hasEntry;
            reach = unite(reach, s.blk);
        }
        a.entryMaybe = maybe;
        a.entryOnly = only;
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
        bool kill = exact && !compound;   // a compound write keeps part of the old value
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
                s.all = unite(s.all, {id});
                if (!nonblocking) s.blk = unite(s.blk, {id});
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
        const ValueSymbol* sym;
        Range r;
        bool exact;
        if (pathOf(e, sym, r, exact)) noteRead(*sym, r, exact, e.sourceRange.start(), &e);
        else this->visitExpr(e);
    }
    void handle(const NamedValueExpression& e) {
        noteRead(e.symbol, {0, widthOf(e.symbol) - 1}, true, e.sourceRange.start(), &e);
    }
    void handle(const HierarchicalValueExpression& e) {
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
            case UnaryOperator::Postdecrement:
                this->visit(e.operand());
                writeTarget(e.operand(), false, true);
                break;
            default:
                this->visitExpr(e);
        }
    }

    void handle(const CallExpression& c) {
        if (!c.isSystemCall()) markExposed();
        this->visitExpr(c);
    }

    // ---- statements: attach the path guards, flag timing, record declaration initialisers
    template <typename T>
        requires(std::is_base_of_v<Statement, T> && !std::is_same_v<T, Statement>)
    void handle(const T& s) {
        auto* saved = curGuards;
        if (auto* g = guardMap.of(&s)) curGuards = g;
        if constexpr (IsAnyOf<T, TimedStatement, WaitStatement, WaitOrderStatement, WaitForkStatement>) {
            if (&s != rootStmt) markOpaque();
        }
        if constexpr (std::is_same_v<T, ContinueStatement>) f.approximated = true;   // slang's base analysis drops the continue path
        if constexpr (std::is_same_v<T, CaseStatement>) {
            // slang counts items that cannot match (x/z bits, values wider than the selector) towards full coverage
            if (s.condition == CaseStatementCondition::Normal && s.expr.type) {
                uint64_t sw = s.expr.type->getBitWidth();
                for (auto& item : s.items)
                    for (auto* e : item.expressions) {
                        ConstantValue cv = e->eval(ec);
                        if (cv.isInteger() && (cv.integer().hasUnknown() || cv.integer().getMinRepresentedBits() > sw)) f.approximated = true;
                    }
            }
        }
        this->visitStmt(s);
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

Flow::Flow(const ProceduralBlockSymbol& p) : proc(p), impl_(std::make_unique<Impl>(*this, p)) {
    impl_->run(p.getBody());
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
