#include "analysis/temps.h"

#include <algorithm>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "sverity/lint.h"

namespace sverity::analysis {

using namespace slang;
using namespace slang::ast;

namespace {

bool tracked(const Symbol& s) { return s.kind == SymbolKind::Variable || s.kind == SymbolKind::Net; }

using Def = std::set<const ValueSymbol*>;
struct State {
    Def def;             // variables written in full on every path to here
    bool dead = false;   // no path reaches here (after break, continue, return, disable)
};

// The state after two paths meet.
void join(State& a, const State& b) {
    if (b.dead) return;
    if (a.dead) { a = b; return; }
    Def both;
    std::set_intersection(a.def.begin(), a.def.end(), b.def.begin(), b.def.end(), std::inserter(both, both.begin()));
    a.def = std::move(both);
}

void wholeTargets(const Expression& e, std::vector<const ValueSymbol*>& out);
void targetRoots(const Expression& e, std::vector<const ValueSymbol*>& out);

// Reads and writes inside expressions. `force` exposes every variable it meets (an event control, a construct not followed).
struct Uses : ASTVisitor<Uses, VisitFlags::AllGood> {
    BodyReads& out;
    const State& st;
    bool force;
    Uses(BodyReads& o, const State& s, bool f) : out(o), st(s), force(f) {}

    void read(const ValueSymbol& s, bool hierarchical = false) {
        if (!tracked(s)) return;
        out.read.insert(&s);
        if (force || hierarchical || !st.def.count(&s)) out.exposed.insert(&s);
    }
    void handle(const NamedValueExpression& e) { read(e.symbol); }
    void handle(const HierarchicalValueExpression& e) { read(e.symbol, true); }
    // The variables a target writes; the indexes inside it are reads.
    void target(const Expression& e, bool alsoRead) {
        switch (e.kind) {
            case ExpressionKind::NamedValue:
            case ExpressionKind::HierarchicalValue: {
                const auto& s = e.kind == ExpressionKind::NamedValue ? e.as<NamedValueExpression>().symbol : e.as<HierarchicalValueExpression>().symbol;
                if (!tracked(s)) return;
                out.written.insert(&s);
                if (alsoRead || force || e.kind == ExpressionKind::HierarchicalValue) read(s, e.kind == ExpressionKind::HierarchicalValue);
                return;
            }
            case ExpressionKind::ElementSelect:
                target(e.as<ElementSelectExpression>().value(), alsoRead);
                e.as<ElementSelectExpression>().selector().visit(*this);
                return;
            case ExpressionKind::RangeSelect:
                target(e.as<RangeSelectExpression>().value(), alsoRead);
                e.as<RangeSelectExpression>().left().visit(*this);
                e.as<RangeSelectExpression>().right().visit(*this);
                return;
            case ExpressionKind::MemberAccess: target(e.as<MemberAccessExpression>().value(), alsoRead); return;
            case ExpressionKind::Conversion: target(e.as<ConversionExpression>().operand(), alsoRead); return;
            case ExpressionKind::Concatenation:
                for (auto* op : e.as<ConcatenationExpression>().operands()) target(*op, alsoRead);
                return;
            default: e.visit(*this);   // not a variable target: its parts are reads
        }
    }
    void handle(const AssignmentExpression& a) {
        if (a.isLValueArg()) { target(a.left(), false); return; }
        a.right().visit(*this);
        if (a.isNonBlocking()) {
            // a select of a variable is a nonblocking write of it too: take every root of the target
            std::vector<const ValueSymbol*> roots;
            targetRoots(a.left(), roots);
            for (auto* w : roots) out.nonblocking.insert(w);
            target(a.left(), a.isCompound());
            return;
        }
        target(a.left(), a.isCompound());
    }
    void handle(const UnaryExpression& u) {
        if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postincrement ||
            u.op == UnaryOperator::Postdecrement)
            target(u.operand(), true);
        else visitDefault(u);
    }
};

// The variables an assignment target replaces in full: a plain name, alone or in a concatenation.
void wholeTargets(const Expression& e, std::vector<const ValueSymbol*>& out) {
    switch (e.kind) {
        case ExpressionKind::NamedValue: {
            const auto& s = e.as<NamedValueExpression>().symbol;
            if (tracked(s)) out.push_back(&s);
            return;
        }
        case ExpressionKind::Conversion: wholeTargets(e.as<ConversionExpression>().operand(), out); return;
        case ExpressionKind::Concatenation:
            for (auto* op : e.as<ConcatenationExpression>().operands()) wholeTargets(*op, out);
            return;
        default: return;
    }
}

// Every variable an assignment target writes in whole or in part: through selects, members, conversions and concatenations.
void targetRoots(const Expression& e, std::vector<const ValueSymbol*>& out) {
    switch (e.kind) {
        case ExpressionKind::NamedValue: {
            const auto& s = e.as<NamedValueExpression>().symbol;
            if (tracked(s)) out.push_back(&s);
            return;
        }
        case ExpressionKind::ElementSelect: targetRoots(e.as<ElementSelectExpression>().value(), out); return;
        case ExpressionKind::RangeSelect: targetRoots(e.as<RangeSelectExpression>().value(), out); return;
        case ExpressionKind::MemberAccess: targetRoots(e.as<MemberAccessExpression>().value(), out); return;
        case ExpressionKind::Conversion: targetRoots(e.as<ConversionExpression>().operand(), out); return;
        case ExpressionKind::Concatenation:
            for (auto* op : e.as<ConcatenationExpression>().operands()) targetRoots(*op, out);
            return;
        case ExpressionKind::Streaming:   // `{<<{a, b}} <= x`: every operand is written (a `with` range only selects within it)
            for (const auto& st : e.as<StreamingConcatenationExpression>().streams()) targetRoots(*st.operand, out);
            return;
        case ExpressionKind::SimpleAssignmentPattern:
            for (auto* op : e.as<SimpleAssignmentPatternExpression>().elements()) targetRoots(*op, out);
            return;
        case ExpressionKind::StructuredAssignmentPattern: {
            const auto& p = e.as<StructuredAssignmentPatternExpression>();
            for (const auto& m : p.memberSetters) targetRoots(*m.expr, out);
            for (const auto& t : p.typeSetters) targetRoots(*t.expr, out);
            for (const auto& i : p.indexSetters) targetRoots(*i.expr, out);
            if (p.defaultSetter) targetRoots(*p.defaultSetter, out);
            return;
        }
        case ExpressionKind::ReplicatedAssignmentPattern:
            for (auto* op : e.as<ReplicatedAssignmentPatternExpression>().elements()) targetRoots(*op, out);
            return;
        case ExpressionKind::Assignment:   // an element of a pattern target: slang wraps each in an output-argument style assignment
            if (e.as<AssignmentExpression>().isLValueArg()) targetRoots(e.as<AssignmentExpression>().left(), out);
            return;
        default: return;
    }
}

struct Walker {
    BodyReads& out;
    State st;
    explicit Walker(BodyReads& o) : out(o) {}

    void use(const Expression& e, bool force = false) { Uses u(out, st, force); e.visit(u); }
    // Anything not followed: every variable it names is exposed, and what was written may be changed by others.
    template <typename T> void opaque(const T& node) {
        Uses u(out, st, true);
        node.visit(u);
        st.def.clear();
    }

    void expression(const Expression& e) {
        if (const auto* a = e.as_if<AssignmentExpression>(); a && !a->isLValueArg()) {
            if (a->timingControl) { opaque(e); return; }
            use(e);
            if (!a->isNonBlocking() && !a->isCompound()) {
                std::vector<const ValueSymbol*> whole;
                wholeTargets(a->left(), whole);
                st.def.insert(whole.begin(), whole.end());
            }
            return;
        }
        use(e);
    }

    void stmt(const Statement& s) {
        switch (s.kind) {
            case StatementKind::Empty: return;
            case StatementKind::List:
                for (auto* c : s.as<StatementList>().list) stmt(*c);
                return;
            case StatementKind::Block: {
                const auto& b = s.as<BlockStatement>();
                if (b.blockKind == StatementBlockKind::Sequential) stmt(b.body);
                else opaque(s);
                return;
            }
            case StatementKind::ExpressionStatement: expression(s.as<ExpressionStatement>().expr); return;
            case StatementKind::VariableDeclaration: {
                const auto& v = s.as<VariableDeclStatement>().symbol;
                if (const auto* init = v.getInitializer()) {
                    use(*init);
                    if (v.lifetime == VariableLifetime::Automatic && tracked(v)) st.def.insert(&v);
                }
                return;
            }
            case StatementKind::Return:
                if (const auto* e = s.as<ReturnStatement>().expr) use(*e);
                st.dead = true;
                return;
            case StatementKind::Continue:
            case StatementKind::Break:
            case StatementKind::Disable: st.dead = true; return;
            case StatementKind::Conditional: {
                const auto& c = s.as<ConditionalStatement>();
                for (const auto& cond : c.conditions) {
                    if (cond.pattern) { opaque(s); return; }
                    use(*cond.expr);
                }
                const State pre = st;
                stmt(c.ifTrue);
                State a = st;
                st = pre;
                if (c.ifFalse) stmt(*c.ifFalse);
                join(a, st);
                st = a;
                return;
            }
            case StatementKind::Case: {
                const auto& c = s.as<CaseStatement>();
                use(c.expr);
                for (const auto& item : c.items)
                    for (auto* e : item.expressions) use(*e);
                const State pre = st;
                State all;
                all.dead = true;
                for (const auto& item : c.items) {
                    st = pre;
                    stmt(*item.stmt);
                    join(all, st);
                }
                st = pre;
                if (c.defaultCase) stmt(*c.defaultCase);
                join(all, st);   // the default arm, or no arm matched
                st = all;
                return;
            }
            case StatementKind::ForLoop: {
                const auto& f = s.as<ForLoopStatement>();
                for (auto* v : f.loopVars) {
                    if (const auto* init = v->getInitializer()) use(*init);
                    if (tracked(*v)) st.def.insert(v);
                }
                for (auto* e : f.initializers) expression(*e);
                const State afterInit = st;
                if (f.stopExpr) use(*f.stopExpr);
                stmt(f.body);
                st = afterInit;   // the steps run after the body, from any point of it: only what the initialisers wrote is certain
                for (auto* e : f.steps) expression(*e);
                st = afterInit;
                return;
            }
            case StatementKind::WhileLoop: {
                const auto& w = s.as<WhileLoopStatement>();
                use(w.cond);
                const State pre = st;
                stmt(w.body);
                st = pre;
                return;
            }
            case StatementKind::DoWhileLoop: {
                const auto& w = s.as<DoWhileLoopStatement>();
                const State pre = st;
                stmt(w.body);
                st = pre;
                use(w.cond);
                return;
            }
            case StatementKind::RepeatLoop: {
                const auto& r = s.as<RepeatLoopStatement>();
                use(r.count);
                const State pre = st;
                stmt(r.body);
                st = pre;
                return;
            }
            case StatementKind::ForeverLoop: {
                const State pre = st;
                stmt(s.as<ForeverLoopStatement>().body);
                st = pre;
                return;
            }
            case StatementKind::ForeachLoop: {
                const State pre = st;
                stmt(s.as<ForeachLoopStatement>().body);
                st = pre;
                return;
            }
            case StatementKind::Timed: {
                const auto& t = s.as<TimedStatement>();
                Uses u(out, st, true);
                t.timing.visit(u);   // an event control reads outside the data flow
                st.def.clear();      // other processes run while it waits
                stmt(t.stmt);
                return;
            }
            default: opaque(s); return;
        }
    }
};

// ---- the design: which variables are state

struct DesignScan : lint::HardwareVisitor<DesignScan, VisitFlags::AllCanonical> {
    void handle(const GenerateBlockSymbol& g) { if (!g.isUninstantiated) visitDefault(g); }
    void handle(const InstanceBodySymbol& body) {
        if (!body.flags.has(InstanceFlags::Uninstantiated)) visitDefault(body);
    }
    std::set<const ValueSymbol*> state, read, written;
    // A flop drives what an edge-triggered procedure writes, and so does any nonblocking write: that variable holds a value.

    template <typename T> void body(const T& node, const Statement& s) {
        (void)node;
        BodyReads r = readsOf(s);
        read.insert(r.read.begin(), r.read.end());
        written.insert(r.written.begin(), r.written.end());
        for (const auto* v : r.exposed) state.insert(v);
        for (const auto* v : r.nonblocking) state.insert(v);
    }
    void handle(const ProceduralBlockSymbol& p) { body(p, p.getBody()); }
    void handle(const SubroutineSymbol& r) { body(r, r.getBody()); }
    // Everything else that names a variable is outside any procedure: a read or write there makes it state.
    void handle(const NamedValueExpression& e) { state.insert(&e.symbol); }
    void handle(const HierarchicalValueExpression& e) { state.insert(&e.symbol); }
    void handle(const PortSymbol& p) {
        if (p.internalSymbol && p.internalSymbol->isValue()) state.insert(&p.internalSymbol->as<ValueSymbol>());
        visitDefault(p);
    }
    void handle(const InstanceSymbol& inst) {
        for (auto* pc : inst.getPortConnections())
            if (const Expression* ex = pc->getExpression()) ex->visit(*this);
        visitDefault(inst);
    }
    void handle(const PrimitiveInstanceSymbol& prim) {
        for (auto* ex : prim.getPortConnections()) ex->visit(*this);
        visitDefault(prim);
    }
};

bool inInterface(const Symbol& s) {
    for (const Scope* sc = s.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
        if (sc->asSymbol().kind == SymbolKind::InstanceBody)
            return sc->asSymbol().as<InstanceBodySymbol>().getDefinition().definitionKind == DefinitionKind::Interface;
    return false;
}

} // namespace

BodyReads readsOf(const Statement& body) {
    BodyReads out;
    Walker w(out);
    w.stmt(body);
    return out;
}

std::unordered_set<const ValueSymbol*> procedureTemporaries(Compilation& compilation) {
    DesignScan scan;
    for (const auto& member : compilation.getRoot().members()) {
        if (const auto* inst = member.as_if<InstanceSymbol>(); inst && inst->body.flags.has(InstanceFlags::Uninstantiated)) continue;
        member.visit(scan);
    }
    std::unordered_set<const ValueSymbol*> temps;
    for (const auto* v : scan.read) {
        if (scan.state.count(v) || v->kind != SymbolKind::Variable || !scan.written.count(v)) continue;
        const auto* sc = v->getParentScope();
        if (!sc) continue;
        const auto k = sc->asSymbol().kind;
        if (k != SymbolKind::InstanceBody && k != SymbolKind::GenerateBlock) continue;
        if (inInterface(*v)) continue;
        temps.insert(v);
    }
    return temps;
}

} // namespace sverity::analysis
