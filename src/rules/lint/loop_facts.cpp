#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_set>

#include "rules/lint/loop_facts.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace sverity::lint {

using namespace slang;
using namespace slang::ast;

const Expression& unwrapImplicit(const Expression& e) {
    const Expression* p = &e;
    while (p->kind == ExpressionKind::Conversion && p->as<ConversionExpression>().isImplicit())
        p = &p->as<ConversionExpression>().operand();
    return *p;
}

std::optional<int64_t> constantInt(const Lint& l, const Expression& e) {
    auto cv = l.evalConst(e);
    if (!cv.isInteger() || cv.integer().hasUnknown()) return std::nullopt;
    return cv.integer().as<int64_t>();
}

std::optional<int64_t> loopStep(const ForLoopStatement& loop, const ValueSymbol* var, Lint& l) {
    if (loop.steps.size() != 1) return std::nullopt;
    const Expression& step = *loop.steps[0];
    const auto isVar = [&](const Expression& e) { return e.kind == ExpressionKind::NamedValue && &e.as<NamedValueExpression>().symbol == var; };
    if (step.kind == ExpressionKind::UnaryOp) {
        const auto& u = step.as<UnaryExpression>();
        if (!isVar(u.operand())) return std::nullopt;
        if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement) return 1;
        if (u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) return -1;
        return std::nullopt;
    }
    if (step.kind != ExpressionKind::Assignment) return std::nullopt;
    const auto& a = step.as<AssignmentExpression>();
    if (!isVar(a.left())) return std::nullopt;
    const auto signedStep = [](std::optional<int64_t> amount, bool subtract) -> std::optional<int64_t> {
        if (!amount || *amount <= 0) return std::nullopt;
        return subtract ? -*amount : *amount;
    };
    if (!a.op) {   // `i = i + c` or `i = i - c`
        const Expression& rhs = unwrapImplicit(a.right());
        if (rhs.kind != ExpressionKind::BinaryOp) return std::nullopt;
        const auto& bin = rhs.as<BinaryExpression>();
        if (!isVar(unwrapImplicit(bin.left()))) return std::nullopt;
        if (bin.op != BinaryOperator::Add && bin.op != BinaryOperator::Subtract) return std::nullopt;
        return signedStep(constantInt(l, bin.right()), bin.op == BinaryOperator::Subtract);
    }
    // slang stores `i += c` with its right-hand side as `<lvalue reference> + c`.
    const Expression* rhs = &unwrapImplicit(a.right());
    if (rhs->kind == ExpressionKind::BinaryOp) {
        const auto& left = unwrapImplicit(rhs->as<BinaryExpression>().left());
        if (left.kind == ExpressionKind::LValueReference || isVar(left)) rhs = &rhs->as<BinaryExpression>().right();
    }
    if (*a.op != BinaryOperator::Add && *a.op != BinaryOperator::Subtract) return std::nullopt;
    return signedStep(constantInt(l, *rhs), *a.op == BinaryOperator::Subtract);
}

bool writesVariable(const Statement& body, const ValueSymbol* var, bool declaredByLoop) {
    struct W : ASTVisitor<W, VisitFlags::AllGood> {
        const ValueSymbol* var; bool found = false; bool local;
        W(const ValueSymbol* v, bool isLocal) : var(v), local(isLocal) {}
        bool names(const Expression& e) const {
            bool hit = false;
            e.visitSymbolReferences([&](const Expression&, const Symbol& s) { hit = hit || &s == var; });
            return hit;
        }
        // The variable an assignment target writes: an index inside the target is only read.
        bool targets(const Expression& e) const {
            const Expression& x = unwrapImplicit(e);
            switch (x.kind) {
                case ExpressionKind::NamedValue: return &x.as<NamedValueExpression>().symbol == var;
                case ExpressionKind::ElementSelect: return targets(x.as<ElementSelectExpression>().value());
                case ExpressionKind::RangeSelect: return targets(x.as<RangeSelectExpression>().value());
                case ExpressionKind::MemberAccess: return targets(x.as<MemberAccessExpression>().value());
                case ExpressionKind::Concatenation:
                    for (auto* op : x.as<ConcatenationExpression>().operands()) if (targets(*op)) return true;
                    return false;
                default: return names(x);   // anything else: any name in it may be written
            }
        }
        void handle(const AssignmentExpression& a) { found = found || targets(a.left()); visitDefault(a); }
        std::set<const SubroutineSymbol*> seen;   // callees already read
        // A module variable is written by a callee whose body (or whose callees' bodies) assigns it; one that cannot be read may.
        void calls(const CallExpression& c) {
            const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
            if (!sub || !*sub || (*sub)->flags.has(MethodFlags::DPIImport) || (*sub)->flags.has(MethodFlags::Pure) ||
                (*sub)->flags.has(MethodFlags::InterfaceExtern) || (*sub)->isVirtual()) {
                found = true;
                return;
            }
            if (seen.insert(*sub).second) (*sub)->getBody().visit(*this);
        }
        // A call may write the variable through an output, inout or ref argument.
        void handle(const CallExpression& c) {
            // A user call can mutate a module variable without an output argument; it cannot name a variable declared by the loop.
            if (!c.isSystemCall() && !local) calls(c);
            for (auto* arg : c.arguments())
                found = found || (writesThrough(c, *arg) && names(*arg));
            visitDefault(c);
        }
        static bool writesThrough(const CallExpression& c, const Expression& arg) {
            if (c.isSystemCall()) return arg.kind == ExpressionKind::Assignment || arg.kind == ExpressionKind::LValueReference;
            const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
            if (!sub || !*sub) return true;
            const auto args = (*sub)->getArguments();
            for (size_t i = 0; i < args.size() && i < c.arguments().size(); i++)
                if (c.arguments()[i] == &arg) return args[i]->direction != ArgumentDirection::In;
            return true;
        }
        void handle(const UnaryExpression& u) {
            const bool step = u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                              u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement;
            found = found || (step && names(u.operand()));
            visitDefault(u);
        }
    } w(var, declaredByLoop);
    body.visit(w);
    return w.found;
}

bool callsSubroutine(const Expression& e) {
    struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
        bool found = false;
        void handle(const CallExpression& c) { found = found || !c.isSystemCall(); visitDefault(c); }
    } calls;
    e.visit(calls);
    return calls.found;
}

// Initializer evaluation must not hide writes behind a supposedly constant counter start.
// User calls can also mutate module state without an output argument, so require call-free RHSs.
bool plainLoopInitializers(const ForLoopStatement& loop) {
    struct Effects : ASTVisitor<Effects, VisitFlags::AllGood> {
        bool found = false;
        void handle(const AssignmentExpression&) { found = true; }
        void handle(const UnaryExpression& u) {
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement ||
                u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement) found = true;
            visitDefault(u);
        }
        void handle(const CallExpression& c) {
            if (!c.isSystemCall()) found = true;
            for (auto* arg : c.arguments())
                if (arg->kind == ExpressionKind::Assignment || arg->kind == ExpressionKind::LValueReference) found = true;
            visitDefault(c);
        }
    } effects;
    std::set<const ValueSymbol*> initialized;
    for (auto* v : loop.loopVars) {
        if (!v->getInitializer()) return false;
        v->getInitializer()->visit(effects);
        initialized.insert(v);
    }
    for (auto* e : loop.initializers) {
        if (e->kind != ExpressionKind::Assignment) return false;
        const auto& a = e->as<AssignmentExpression>();
        if (a.isNonBlocking() || a.isCompound() || a.left().kind != ExpressionKind::NamedValue) return false;
        if (!initialized.insert(&a.left().as<NamedValueExpression>().symbol).second) return false;
        a.right().visit(effects);
    }
    return !effects.found;
}

bool neverRuns(const ForLoopStatement& loop, Lint& l) {
    if (!plainLoopInitializers(loop)) return false;
    if (!loop.stopExpr) return false;
    ASTContext ac(l.compilation().getRoot(), LookupLocation::max);
    EvalContext ec(ac);
    std::set<const Symbol*> known;
    for (auto* v : loop.loopVars) {
        if (!v->getInitializer()) return false;
        auto cv = l.evalConst(*v->getInitializer());
        if (!cv) return false;
        ec.createLocal(v, cv);
        known.insert(v);
    }
    for (auto* e : loop.initializers) {
        if (e->kind != ExpressionKind::Assignment) return false;
        const auto& a = e->as<AssignmentExpression>();
        if (a.isNonBlocking() || a.isCompound() || a.left().kind != ExpressionKind::NamedValue) return false;
        auto cv = l.evalConst(a.right());
        if (!cv) return false;
        ec.createLocal(&a.left().as<NamedValueExpression>().symbol, cv);
        known.insert(&a.left().as<NamedValueExpression>().symbol);
    }
    bool onlyKnown = true;   // anything else (a port, a variable) has no value here
    loop.stopExpr->visitSymbolReferences([&](const Expression&, const Symbol& sym) {
        if (sym.kind != SymbolKind::Parameter && sym.kind != SymbolKind::EnumValue && !known.count(&sym)) onlyKnown = false;
    });
    if (!onlyKnown) return false;
    if (callsSubroutine(*loop.stopExpr)) return false;
    auto t = loop.stopExpr->eval(ec);
    return t.isInteger() && !t.integer().hasUnknown() && t.integer() == 0;
}

std::optional<LoopValues> constantLoopValues(const ForLoopStatement& loop, Lint& l, bool allowWrap) {
    if (!plainLoopInitializers(loop) || !loop.stopExpr || loop.stopExpr->kind != ExpressionKind::BinaryOp) return std::nullopt;
    const auto& stop = loop.stopExpr->as<BinaryExpression>();
    const auto& operand = unwrapImplicit(stop.left());
    if (operand.kind != ExpressionKind::NamedValue) return std::nullopt;
    const auto* var = &operand.as<NamedValueExpression>().symbol;
    const auto bound = constantInt(l, stop.right());
    const auto step = loopStep(loop, var, l);
    const bool declared = std::find(loop.loopVars.begin(), loop.loopVars.end(), var) != loop.loopVars.end();
    if (!bound || !step || writesVariable(loop.body, var, declared)) return std::nullopt;
    std::optional<int64_t> start;   // declared with the loop, or assigned by its initializers
    for (auto* v : loop.loopVars)
        if (v == var && v->getInitializer()) start = constantInt(l, *v->getInitializer());
    for (auto* e : loop.initializers)
        if (e->kind == ExpressionKind::Assignment) {
            const auto& a = e->as<AssignmentExpression>();
            if (a.left().kind == ExpressionKind::NamedValue && &a.left().as<NamedValueExpression>().symbol == var)
                start = constantInt(l, a.right());
        }
    if (!start) return std::nullopt;
    const int64_t f = *start, b = *bound, s = *step;
    // Only remove comparison conversions that preserve every source value. A signed variable compared as unsigned keeps
    // its values when it never goes below zero: it starts at zero or more and steps up, towards a bound of zero or more.
    if (operand.type != stop.left().type) {
        const auto& from = *operand.type;
        const auto& to = *stop.left().type;
        const bool nonNegative = f >= 0 && s > 0 && b >= 0;
        if (!from.isIntegral() || !to.isIntegral() || to.getBitWidth() < from.getBitWidth() ||
            (from.isSigned() && !to.isSigned() && !nonNegative) ||
            (!from.isSigned() && to.isSigned() && to.getBitWidth() == from.getBitWidth())) return std::nullopt;
    }
    uint64_t count = 0;   // values f + k*s that satisfy the condition, counted in closed form
    if (stop.op == BinaryOperator::LessThan && s > 0 && f < b) count = static_cast<uint64_t>((b - f + s - 1) / s);
    else if (stop.op == BinaryOperator::LessThanEqual && s > 0 && f <= b) count = static_cast<uint64_t>((b - f) / s + 1);
    else if (stop.op == BinaryOperator::GreaterThan && s < 0 && f > b) count = static_cast<uint64_t>((f - b - s - 1) / -s);
    else if (stop.op == BinaryOperator::GreaterThanEqual && s < 0 && f >= b) count = static_cast<uint64_t>((f - b) / -s + 1);
    if (count == 0) return std::nullopt;
    // Every value the counter takes, up to and including the one that ends the loop, must fit the counter's own type: a counter
    // that wraps (`byte i < 8'd200`, `bit [2:0] i < 8`, an unsigned `i >= 0`) does not step through the values computed here.
    const auto& vt = var->getType();
    const uint32_t w = static_cast<uint32_t>(vt.getBitWidth());
    if (w == 0 || w > 64) return std::nullopt;
    const __int128 lo = vt.isSigned() ? -(static_cast<__int128>(1) << (w - 1)) : 0;
    const __int128 hi = vt.isSigned() ? (static_cast<__int128>(1) << (w - 1)) - 1 : (static_cast<__int128>(1) << w) - 1;
    const __int128 last = static_cast<__int128>(f) + static_cast<__int128>(count) * s;
    if (f < lo || f > hi) return std::nullopt;
    if (last < lo || last > hi) {
        if (!allowWrap) return std::nullopt;
        // The iterations before the counter leaves its type still run in order: keep only those.
        const __int128 fitting = (s > 0 ? (hi - f) / s : (f - lo) / -static_cast<__int128>(s)) + 1;
        count = static_cast<uint64_t>(std::min<__int128>(fitting, count));
        return LoopValues{var, f, s, count, true};
    }
    return LoopValues{var, f, s, count};
}

namespace {

// ---- three-valued evaluation of an exit condition, one loop iteration at a time
//  Known   the value is fixed by the iteration alone (constants, parameters, the loop variable, locals computed from them,
//          pure automatic callees of them);
//  Design  it depends on design state that is unknown at lint time (a module-level variable or net, a port, an interface signal,
//          a callee whose body reads one, a local copied from one);
//  Iter    it depends on a loop value this analysis does not enumerate (an enclosing loop's counter, a counter with no known range);
//  Fail    it cannot be evaluated: an unreadable callee, an unmodelled construct.
enum class K { Known, Design, Iter, Fail };
struct V {
    K k = K::Fail;
    ConstantValue cv;
};
V known(ConstantValue c) { return {K::Known, std::move(c)}; }
V kind(K k) { return {k, {}}; }
V boolean(bool b) { return known(ConstantValue(SVInt(1, b ? 1 : 0, false))); }
// 0 = false, 1 = true, -1 = no integer truth (an x or z bit, a non-integer)
int truth(const V& v) {
    if (v.k != K::Known || !v.cv.isInteger() || v.cv.integer().hasUnknown()) return -1;
    return v.cv.isTrue() ? 1 : 0;
}
// Worst of two non-decisive operands: nothing is known about the result beyond what each side is.
K merge(K a, K b) {
    if (a == K::Fail || b == K::Fail) return K::Fail;
    if (a == K::Iter || b == K::Iter) return K::Iter;
    if (a == K::Design || b == K::Design) return K::Design;
    return K::Known;
}
V orOf(const V& a, const V& b) {
    if (truth(a) == 1 || truth(b) == 1) return boolean(true);
    if (a.k == K::Known && b.k == K::Known) return truth(a) == 0 && truth(b) == 0 ? boolean(false) : kind(K::Fail);
    return kind(merge(a.k, b.k));
}
V andOf(const V& a, const V& b) {
    if (truth(a) == 0 || truth(b) == 0) return boolean(false);
    if (a.k == K::Known && b.k == K::Known) return truth(a) == 1 && truth(b) == 1 ? boolean(true) : kind(K::Fail);
    return kind(merge(a.k, b.k));
}

bool moduleLevel(const Symbol& sym) {
    const Scope* sc = sym.getParentScope();
    if (!sc) return false;
    switch (sc->asSymbol().kind) {
        case SymbolKind::InstanceBody: case SymbolKind::GenerateBlock: case SymbolKind::CompilationUnit: case SymbolKind::Package: return true;
        default: return false;
    }
}
bool within(const Symbol& sym, const Symbol& outer) {
    for (const Scope* sc = sym.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
        if (&sc->asSymbol() == &outer) return true;
    return false;
}
bool dimensionQuery(std::string_view name) {
    return name == "$bits" || name == "$size" || name == "$dimensions" || name == "$unpacked_dimensions" || name == "$left" || name == "$right" ||
           name == "$low" || name == "$high" || name == "$increment" || name == "$typename";
}
bool stepOf(const UnaryExpression& u) {
    return u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Predecrement || u.op == UnaryOperator::Postdecrement;
}
// The variable an assignment target writes (an index inside it is only read), through selects and concatenations.
bool targets(const Expression& e, const Symbol& var) {
    const Expression& x = unwrapImplicit(e);
    switch (x.kind) {
        case ExpressionKind::NamedValue: return &x.as<NamedValueExpression>().symbol == &var;
        case ExpressionKind::ElementSelect: return targets(x.as<ElementSelectExpression>().value(), var);
        case ExpressionKind::RangeSelect: return targets(x.as<RangeSelectExpression>().value(), var);
        case ExpressionKind::MemberAccess: return targets(x.as<MemberAccessExpression>().value(), var);
        case ExpressionKind::Concatenation:
            for (auto* op : x.as<ConcatenationExpression>().operands()) if (targets(*op, var)) return true;
            return false;
        default: return false;
    }
}
bool readable(const SubroutineSymbol& sub) {
    return !sub.flags.has(MethodFlags::DPIImport) && !sub.flags.has(MethodFlags::Pure) && !sub.flags.has(MethodFlags::InterfaceExtern) &&
           !sub.isVirtual() && sub.defaultLifetime == VariableLifetime::Automatic;
}

// A call frame of the exit being judged: the formals of the callee whose body holds it, bound to the arguments of the call.
struct Frame {
    const SubroutineSymbol* sub = nullptr;
    std::map<const Symbol*, const Expression*> args;
    std::shared_ptr<Frame> parent;
};
using FramePtr = std::shared_ptr<Frame>;

// One condition that must hold for an exit to run.
struct Cond {
    enum Kind { Expr, CaseArm, Unknown } kind = Unknown;
    bool negate = false;                          // the else arm of a single condition
    const Expression* e = nullptr;                // the condition, or the case selector
    std::vector<const Expression*> items;         // the case arm's item expressions
    std::vector<const Expression*> prior;         // the items of the arms before it: the first arm that matches is the one taken
    FramePtr frame;
};
struct Exit {
    std::vector<Cond> conds;
    bool opaque = false;
    bool isContinue = false;   // a `continue` of the loop under test (slang's flow analysis drops everything after it)
    bool isBreak = false;      // a `break` of the loop under test
    slang::SourceLocation loc;
};

struct Walker;

// The reads and writes in a callee's body that decide whether it is a function of its arguments alone.
struct Body : ASTVisitor<Body, VisitFlags::AllGood> {
    Walker& o;
    const SubroutineSymbol& sub;
    bool design = false, unknown = false;
    Body(Walker& owner, const SubroutineSymbol& s) : o(owner), sub(s) {}
    void ref(const Symbol& sym);
    void handle(const NamedValueExpression& e) { ref(e.symbol); }
    void handle(const HierarchicalValueExpression&) { design = true; }
    void handle(const TimedStatement&) { unknown = true; }
    void handle(const AssignmentExpression& a);
    void handle(const UnaryExpression& u);
    void handle(const CallExpression& c);
};

// The writes to one local variable of a scope, other than its declaration's initialiser.
struct Writes : ASTVisitor<Writes, VisitFlags::AllGood> {
    Walker& o;
    const Symbol& var;
    std::vector<const Expression*> rhs;   // the values written
    bool designViaCall = false, opaque = false;
    int count = 0;
    Writes(Walker& owner, const Symbol& v) : o(owner), var(v) {}
    void handle(const AssignmentExpression& a) {
        if (targets(a.left(), var)) {
            count++;
            if (a.isLValueArg()) opaque = true;   // reached through a call: handled at the call
            else rhs.push_back(&a.right());
        }
        visitDefault(a);
    }
    void handle(const UnaryExpression& u) {
        if (stepOf(u) && targets(u.operand(), var)) count++;
        visitDefault(u);
    }
    void handle(const CallExpression& c);
};

struct Walker : ASTVisitor<Walker, VisitFlags::AllGood> {
    const Lint* lint = nullptr;
    bool found = false;
    bool wholeProcedure = false;
    SourceRange region;                          // the statement under test (a disable of a scope inside it does not leave)
    const Statement* regionBody = nullptr;
    const Statement* scope = nullptr;            // the procedure or subroutine around the statement, for locals declared outside it
    const SubroutineSymbol* inside = nullptr;    // the subroutine whose body is being read; its own scopes are no exit
    int loopDepth = 0;
    std::vector<Cond> conds;                     // the conditions around the node being visited
    std::vector<Exit> exits;
    FramePtr frame;
    std::unordered_set<const SubroutineSymbol*> active, reading;

    // ---- evaluation environment (set per iteration)
    const ValueSymbol* counter = nullptr;        // the loop variable of the statement's loop, with its value in `counterValue`
    std::optional<int64_t> counterValue;
    std::vector<const ValueSymbol*> otherVars;   // counters in scope whose values are not enumerated
    std::map<const ValueSymbol*, int64_t> outerValue;   // enclosing counters with a value in the combination being judged (exitProfile)
    std::unordered_set<const Symbol*> inProgress;

    void exit(bool opaque = false, bool isContinue = false, bool isBreak = false, slang::SourceLocation loc = {}) {
        found = true;
        exits.push_back({conds, opaque, isContinue, isBreak, loc});
    }

    bool declaredInRegion(const Symbol& sym) const {
        return sym.location.buffer() == region.start().buffer() && sym.location >= region.start() && sym.location <= region.end();
    }

    // ---- what a callee's body reads
    struct BodyInfo { bool design = false, unknown = false; };
    BodyInfo bodyOf(const SubroutineSymbol& sub) {
        if (!readable(sub) || !reading.insert(&sub).second) return {false, true};
        Body b(*this, sub);
        sub.getBody().visit(b);
        reading.erase(&sub);
        return {b.design, b.unknown};
    }

    // ---- values
    // Whether a value (an initialiser or an assignment's right-hand side, or an argument) reads design state, through locals too.
    bool readsDesign(const Expression& e) {
        struct R : ASTVisitor<R, VisitFlags::AllGood> {
            Walker& o; bool design = false;
            explicit R(Walker& owner) : o(owner) {}
            void handle(const NamedValueExpression& e) { design = design || o.symbolReadsDesign(e.symbol); }
            void handle(const HierarchicalValueExpression&) { design = true; }
            void handle(const CallExpression& c) {
                if (!c.isSystemCall())
                    if (const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine); sub && *sub && o.bodyOf(**sub).design) design = true;
                visitDefault(c);
            }
        } r(*this);
        e.visit(r);
        return r.design;
    }
    bool symbolReadsDesign(const Symbol& sym) {
        if (!sym.isValue()) return false;
        switch (sym.kind) {
            case SymbolKind::Parameter: case SymbolKind::EnumValue: case SymbolKind::Specparam: case SymbolKind::Genvar: return false;
            default: break;
        }
        if (sym.kind == SymbolKind::FormalArgument) return false;   // judged at its call
        if (moduleLevel(sym)) return true;
        if (!inProgress.insert(&sym).second) return false;
        bool design = false;
        if (const auto* v = sym.as_if<VariableSymbol>()) {
            if (v->getInitializer() && readsDesign(*v->getInitializer())) design = true;
            const Statement* root = inside && within(sym, *inside) ? &inside->getBody() : declaredInRegion(sym) && regionBody ? regionBody : scope;
            if (root && !design) {
                Writes w(*this, sym);
                root->visit(w);
                for (auto* r : w.rhs) design = design || readsDesign(*r);
                design = design || w.designViaCall;
            }
        }
        inProgress.erase(&sym);
        return design;
    }

    struct Eval {
        Walker& o;
        explicit Eval(Walker& owner) : o(owner) {}
        V symbolValue(const Symbol& sym, const FramePtr& fr) {
            switch (sym.kind) {
                case SymbolKind::Parameter: case SymbolKind::EnumValue: case SymbolKind::Specparam: case SymbolKind::Genvar: return kind(K::Known);   // folded by slang
                default: break;
            }
            if (!sym.isValue()) return kind(K::Fail);
            if (&sym == o.counter) {
                if (!o.counterValue) return kind(K::Iter);
                const Type& t = sym.as<ValueSymbol>().getType();
                return known(ConstantValue(SVInt(static_cast<bitwidth_t>(t.getBitWidth()), static_cast<uint64_t>(*o.counterValue), t.isSigned())));
            }
            if (auto it = o.outerValue.find(static_cast<const ValueSymbol*>(&sym)); it != o.outerValue.end()) {
                const Type& t = sym.as<ValueSymbol>().getType();
                return known(ConstantValue(SVInt(static_cast<bitwidth_t>(t.getBitWidth()), static_cast<uint64_t>(it->second), t.isSigned())));
            }
            if (std::find(o.otherVars.begin(), o.otherVars.end(), &sym) != o.otherVars.end()) return kind(K::Iter);
            if (sym.kind == SymbolKind::FormalArgument) {
                for (FramePtr f = fr; f; f = f->parent) {
                    if (!f->sub || !within(sym, *f->sub)) continue;
                    auto it = f->args.find(&sym);
                    if (it == f->args.end() || sym.as<FormalArgumentSymbol>().direction != ArgumentDirection::In) return kind(K::Fail);
                    return ev(*it->second, f->parent);
                }
                return kind(K::Fail);
            }
            if (moduleLevel(sym)) return kind(K::Design);
            const auto* v = sym.as_if<VariableSymbol>();
            if (!v) return kind(K::Fail);
            // a local: an automatic one written only by its initialiser is that value in each iteration
            const bool mine = (o.inside && within(sym, *o.inside)) || o.declaredInRegion(sym);
            if (mine && v->lifetime == VariableLifetime::Automatic && v->getInitializer()) {
                const Statement* root = o.inside && within(sym, *o.inside) ? &o.inside->getBody() : o.regionBody;
                Writes w(o, sym);
                if (root) root->visit(w);
                if (w.count == 0 && !w.opaque) return ev(*v->getInitializer(), fr);
            }
            // otherwise: run-time when something it is assigned from reads design state, and not evaluated when it does not
            return o.symbolReadsDesign(sym) ? kind(K::Design) : kind(K::Fail);
        }

        // Collects what an expression refers to, to decide whether it can be evaluated as a whole.
        struct Leaves : ASTVisitor<Leaves, VisitFlags::AllGood> {
            Eval& ev; FramePtr fr;
            K k = K::Known;
            bool unsafeForDesign = false;
            std::vector<std::pair<const ValueSymbol*, ConstantValue>> bind;
            Leaves(Eval& e, FramePtr f) : ev(e), fr(std::move(f)) {}
            void handle(const NamedValueExpression& e) {
                if (e.getConstant()) return;
                V v = ev.symbolValue(e.symbol, fr);
                k = merge(k, v.k);
                if (v.k == K::Known && v.cv && e.symbol.isValue()) bind.push_back({&e.symbol.as<ValueSymbol>(), std::move(v.cv)});
            }
            void handle(const HierarchicalValueExpression&) { k = merge(k, K::Design); }
            void handle(const CallExpression& c) {
                if (c.getConstant()) return;
                if (c.isSystemCall()) { if (!dimensionQuery(c.getSubroutineName())) k = merge(k, K::Fail); return; }
                const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
                if (!sub || !*sub || c.thisClass()) { k = merge(k, K::Fail); return; }
                auto info = ev.o.bodyOf(**sub);
                for (auto* f : (*sub)->getArguments()) if (f->direction != ArgumentDirection::In) { k = merge(k, K::Fail); return; }
                if (info.unknown) { k = merge(k, K::Fail); return; }
                if (info.design) k = merge(k, K::Design);
                visitDefault(c);   // the arguments
            }
            // operators whose result an unknown operand cannot be said to decide (a known operand can force it)
            void handle(const BinaryExpression& e) {
                switch (e.op) {
                    case BinaryOperator::Equality: case BinaryOperator::Inequality: case BinaryOperator::LessThan: case BinaryOperator::LessThanEqual:
                    case BinaryOperator::GreaterThan: case BinaryOperator::GreaterThanEqual: case BinaryOperator::Add: case BinaryOperator::Subtract:
                    case BinaryOperator::BinaryXor: case BinaryOperator::BinaryXnor: break;
                    default: unsafeForDesign = true;
                }
                visitDefault(e);
            }
            void handle(const ConditionalExpression& e) { unsafeForDesign = true; visitDefault(e); }
            void handle(const AssignmentExpression&) { k = merge(k, K::Fail); }
            void handle(const UnaryExpression& u) {
                if (stepOf(u)) k = merge(k, K::Fail);
                else if (u.op != UnaryOperator::BitwiseNot && u.op != UnaryOperator::Minus && u.op != UnaryOperator::Plus && u.op != UnaryOperator::LogicalNot) unsafeForDesign = true;
                visitDefault(u);
            }
        };

        V ev(const Expression& e, const FramePtr& fr) {
            if (const ConstantValue* c = e.getConstant()) return known(*c);
            if (e.kind == ExpressionKind::Conversion && e.as<ConversionExpression>().isImplicit()) {
                V inner = ev(e.as<ConversionExpression>().operand(), fr);
                if (inner.k != K::Known) return inner;
            }
            if (e.kind == ExpressionKind::BinaryOp) {
                const auto& b = e.as<BinaryExpression>();
                if (b.op == BinaryOperator::LogicalOr) return orOf(ev(b.left(), fr), ev(b.right(), fr));
                if (b.op == BinaryOperator::LogicalAnd) return andOf(ev(b.left(), fr), ev(b.right(), fr));
            }
            if (e.kind == ExpressionKind::UnaryOp && e.as<UnaryExpression>().op == UnaryOperator::LogicalNot) {
                V inner = ev(e.as<UnaryExpression>().operand(), fr);
                if (inner.k != K::Known) return inner;
            }
            if (e.kind == ExpressionKind::ConditionalOp) {
                const auto& c = e.as<ConditionalExpression>();
                if (c.conditions.size() == 1 && !c.conditions[0].pattern) {
                    V cond = ev(*c.conditions[0].expr, fr);
                    if (truth(cond) == 1) return ev(c.left(), fr);
                    if (truth(cond) == 0) return ev(c.right(), fr);
                    V a = ev(c.left(), fr), b = ev(c.right(), fr);
                    if (cond.k == K::Known) return kind(K::Fail);
                    if (a.k == K::Known && b.k == K::Known && a.cv == b.cv) return a;
                    return kind(merge(cond.k, merge(a.k, b.k)));
                }
                return kind(K::Fail);
            }
            Leaves leaves(*this, fr);
            e.visit(leaves);
            if (leaves.k == K::Fail || leaves.k == K::Iter) return kind(leaves.k);
            if (leaves.k == K::Design) return leaves.unsafeForDesign ? kind(K::Fail) : kind(K::Design);
            // every leaf is known: slang evaluates the whole expression with those values
            ASTContext ac(o.lint->compilation().getRoot(), LookupLocation::max);
            EvalContext ec(ac);
            for (auto& [sym, cv] : leaves.bind) ec.createLocal(sym, cv);
            ConstantValue cv = e.eval(ec);
            return cv ? known(std::move(cv)) : kind(K::Fail);
        }

        V condTruth(const Cond& c) {
            if (c.kind == Cond::Unknown) return kind(K::Fail);
            V sel = ev(*c.e, c.frame);
            if (c.kind == Cond::Expr) {
                if (!c.negate) return sel;
                const int t = truth(sel);
                return t < 0 ? (sel.k == K::Known ? kind(K::Fail) : sel) : boolean(t == 0);
            }
            auto anyOf = [&](const std::vector<const Expression*>& items) {
                V any = boolean(false);
                for (auto* item : items) {
                    V it = ev(*item, c.frame);
                    V eq;
                    if (sel.k == K::Known && it.k == K::Known) {
                        if (!sel.cv.isInteger() || !it.cv.isInteger() || sel.cv.integer().hasUnknown() || it.cv.integer().hasUnknown()) eq = kind(K::Fail);
                        else eq = boolean(bool(sel.cv.integer() == it.cv.integer()));
                    }
                    else eq = kind(merge(sel.k, it.k));
                    any = orOf(any, eq);
                }
                return any;
            };
            V mine = anyOf(c.items);
            if (c.prior.empty()) return mine;
            // the arm runs only when no earlier arm matched
            V before = anyOf(c.prior);
            const int t = truth(before);
            V notBefore = t >= 0 ? boolean(t == 0) : before.k == K::Known ? kind(K::Fail) : before;
            return andOf(mine, notBefore);
        }
    };

    // Whether, in some iteration, some exit certainly runs or cannot be judged (so later writes may never run).
    // `continueStops` is set when such an exit is a `continue`.
    bool someExitMayStop(bool& continueStops) {
        bool stops = false;
        const bool counted = counter && counterValue_count > 0 && counterValue_count <= 4096;
        const uint64_t n = counted ? counterValue_count : 1;
        Eval ev(*this);
        for (uint64_t k = 0; k < n; k++) {
            if (counted) counterValue = counterFirst + static_cast<int64_t>(k) * counterStep;
            else counterValue.reset();
            for (const Exit& x : exits) {
                bool never = false, certain = !x.opaque, unresolved = x.opaque;
                for (const Cond& c : x.conds) {
                    V t = ev.condTruth(c);
                    const int b = truth(t);
                    if (b == 0) { never = true; break; }
                    if (b == 1) continue;
                    certain = false;
                    if (t.k != K::Design) unresolved = true;
                }
                if (never) continue;
                if (certain || unresolved) {
                    stops = true;
                    if (x.isContinue) continueStops = true;
                }
            }
        }
        return stops;
    }
    uint64_t counterValue_count = 0;
    int64_t counterFirst = 0, counterStep = 0;

    template<typename F> void guarded(Cond c, F&& visit) {
        conds.push_back(std::move(c));
        visit();
        conds.pop_back();
    }
    Cond exprCond(const Expression& e) {
        Cond c;
        c.kind = Cond::Expr;
        c.e = &e;
        c.frame = frame;
        return c;
    }
    void handle(const ConditionalStatement& s) {
        std::vector<Cond> cs;
        for (auto& cond : s.conditions) {
            cond.expr->visit(*this);
            if (cond.pattern) { Cond u; cs.push_back(u); }
            else cs.push_back(exprCond(*cond.expr));
        }
        const size_t depth = conds.size();
        for (auto& c : cs) conds.push_back(c);
        s.ifTrue.visit(*this);
        conds.resize(depth);
        if (s.ifFalse) {
            if (cs.size() == 1 && cs[0].kind == Cond::Expr) { cs[0].negate = true; conds.push_back(cs[0]); }
            else conds.push_back(Cond{});   // the negation of several conditions is not tracked
            s.ifFalse->visit(*this);
            conds.resize(depth);
        }
    }
    void handle(const CaseStatement& s) {
        s.expr.visit(*this);
        std::vector<const Expression*> prior;
        for (auto& g : s.items) {
            Cond c;
            c.kind = Cond::CaseArm;
            c.e = &s.expr;
            c.frame = frame;
            c.prior = prior;
            for (auto* e : g.expressions) c.items.push_back(e);
            guarded(std::move(c), [&] { g.stmt->visit(*this); });
            for (auto* e : g.expressions) prior.push_back(e);
        }
        if (s.defaultCase) guarded(Cond{}, [&] { s.defaultCase->visit(*this); });
    }
    void handle(const PatternCaseStatement& s) {
        guarded(Cond{}, [&] {
            for (auto& g : s.items) g.stmt->visit(*this);
            if (s.defaultCase) s.defaultCase->visit(*this);
        });
    }
    template<typename T> void nested(const T& s) {
        ++loopDepth;
        guarded(Cond{}, [&] { visitDefault(s); });   // a nested loop's own condition is not tracked
        --loopDepth;
    }
    void handle(const ForLoopStatement& s) { nested(s); }
    void handle(const WhileLoopStatement& s) { nested(s); }
    void handle(const DoWhileLoopStatement& s) { nested(s); }
    void handle(const RepeatLoopStatement& s) { nested(s); }
    void handle(const ForeverLoopStatement& s) { nested(s); }
    void handle(const ForeachLoopStatement& s) { nested(s); }

    void handle(const BreakStatement& s) { if (!inside && loopDepth == 0) exit(false, false, true, s.sourceRange.start()); }
    void handle(const ContinueStatement& s) { if (!inside && loopDepth == 0) exit(false, true, false, s.sourceRange.start()); }
    void handle(const ReturnStatement&) { if (!inside) exit(); }
    void handle(const DisableForkStatement&) { exit(); }
    void handle(const DisableStatement& d) {
        bool local = false;
        d.target.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
            if (inside) {
                for (const Scope* sc = sym.getParentScope(); sc && !local; sc = sc->asSymbol().getParentScope())
                    local = &sc->asSymbol() == inside;
                local = local || &sym == inside;
            }
            else if (!wholeProcedure && sym.location.buffer() == region.start().buffer() && sym.location >= region.start() && sym.location <= region.end())
                local = true;   // a block inside the loop body: the rest of that block is skipped, the loop goes on
        });
        if (!local) exit();
    }
    void handle(const CallExpression& c) {
        if (!c.isSystemCall()) {
            const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine);
            if (!sub || !*sub || (*sub)->flags.has(MethodFlags::DPIImport) || (*sub)->flags.has(MethodFlags::Pure) ||
                (*sub)->flags.has(MethodFlags::InterfaceExtern) || (*sub)->isVirtual())
                exit(true);
            else if (active.insert(*sub).second) {
                const auto* outerInside = inside;
                auto outerFrame = frame;
                auto fr = std::make_shared<Frame>();
                fr->sub = *sub;
                fr->parent = frame;
                const auto formals = (*sub)->getArguments();
                for (size_t i = 0; i < formals.size() && i < c.arguments().size(); i++) fr->args[formals[i]] = c.arguments()[i];
                frame = fr;
                inside = *sub;
                const int depth = loopDepth;
                loopDepth = 0;
                (*sub)->getBody().visit(*this);
                loopDepth = depth;
                inside = outerInside;
                frame = outerFrame;
                active.erase(*sub);
            }
        }
        visitDefault(c);
    }
};

void Body::ref(const Symbol& s) {
    switch (s.kind) {
        case SymbolKind::Parameter: case SymbolKind::EnumValue: case SymbolKind::Specparam: case SymbolKind::Genvar: case SymbolKind::FormalArgument: return;
        default: break;
    }
    if (!s.isValue()) { unknown = true; return; }
    if (within(s, sub)) { const auto* v = s.as_if<VariableSymbol>(); if (!v || v->lifetime != VariableLifetime::Automatic) unknown = true; return; }
    if (moduleLevel(s)) design = true; else unknown = true;
}
void Body::handle(const AssignmentExpression& a) {
    const Symbol* root = nullptr;
    unwrapImplicit(a.left()).visitSymbolReferences([&](const Expression&, const Symbol& s) { if (!root) root = &s; });
    if (!root || !within(*root, sub)) unknown = true;   // a write to state outside the callee is an effect, not a function of the arguments
    visitDefault(a);
}
void Body::handle(const UnaryExpression& u) {
    if (stepOf(u)) {
        const Symbol* root = nullptr;
        u.operand().visitSymbolReferences([&](const Expression&, const Symbol& s) { if (!root) root = &s; });
        if (!root || !within(*root, sub)) unknown = true;
    }
    visitDefault(u);
}
void Body::handle(const CallExpression& c) {
    if (c.isSystemCall()) { if (!dimensionQuery(c.getSubroutineName())) unknown = true; }
    else {
        const auto* callee = std::get_if<const SubroutineSymbol*>(&c.subroutine);
        if (!callee || !*callee || c.thisClass()) unknown = true;
        else {
            auto info = o.bodyOf(**callee);
            design = design || info.design;
            unknown = unknown || info.unknown;
        }
    }
    visitDefault(c);
}
void Writes::handle(const CallExpression& c) {
    const auto* sub = c.isSystemCall() ? nullptr : std::get_if<const SubroutineSymbol*>(&c.subroutine);
    for (size_t i = 0; i < c.arguments().size(); i++) {
        const Expression& arg = *c.arguments()[i];
        const Expression& inner = arg.kind == ExpressionKind::Assignment ? arg.as<AssignmentExpression>().left() : arg;
        bool names = false;
        inner.visitSymbolReferences([&](const Expression&, const Symbol& s) { names = names || &s == &var; });
        if (!names) continue;
        bool writes = c.isSystemCall() ? arg.kind == ExpressionKind::Assignment || arg.kind == ExpressionKind::LValueReference : true;
        if (sub && *sub) {
            const auto formals = (*sub)->getArguments();
            if (i < formals.size()) writes = formals[i]->direction != ArgumentDirection::In;
        }
        if (!writes) continue;
        count++;
        // the value arrives from the call's other arguments, and from whatever the callee reads
        for (size_t j = 0; j < c.arguments().size(); j++) if (j != i) rhs.push_back(c.arguments()[j]);
        if (!sub || !*sub) opaque = true;
        else if (o.bodyOf(**sub).design) designViaCall = true;
        else if (o.bodyOf(**sub).unknown) opaque = true;
    }
    visitDefault(c);
}

} // namespace

EarlyExitInfo earlyExit(const Lint& l, const Statement& body, const std::optional<LoopValues>& values,
                        const std::vector<const ValueSymbol*>& otherVars, const Statement* scope) {
    Walker w;
    w.lint = &l;
    w.region = body.sourceRange;
    w.regionBody = &body;
    w.scope = scope;
    w.otherVars = otherVars;
    if (values) {
        w.counter = values->var;
        w.counterValue_count = values->count;
        w.counterFirst = values->first;
        w.counterStep = values->step;
    }
    body.visit(w);
    if (!w.found) return {};
    bool continues = false;
    const bool stops = w.someExitMayStop(continues);
    return {true, stops, continues};
}

std::optional<ExitProfile> exitProfile(const Lint& l, const Statement& body, const std::optional<LoopValues>& values,
                                      const std::vector<LoopValues>& outers, const std::vector<const ValueSymbol*>& otherVars, const Statement* scope) {
    if (!values || values->count > 4096) return std::nullopt;
    uint64_t combos = 1;
    for (const auto& o : outers) {
        if (o.count == 0 || combos * o.count > 4096) return std::nullopt;
        combos *= o.count;
    }
    if (combos * values->count > 65536) return std::nullopt;
    Walker w;
    w.lint = &l;
    w.region = body.sourceRange;
    w.regionBody = &body;
    w.scope = scope;
    w.otherVars = otherVars;
    w.counter = values->var;
    body.visit(w);
    ExitProfile out;
    out.values = *values;
    out.outers = outers;
    out.sm = l.compilation().getSourceManager();
    if (!w.found) return out;
    Walker::Eval ev(w);
    for (uint64_t combo = 0; combo < combos; combo++) {
        uint64_t rest = combo;
        std::vector<int64_t> at;
        w.outerValue.clear();
        for (const auto& o : outers) {
            at.push_back(o.at(rest % o.count));
            w.outerValue[o.var] = at.back();
            rest /= o.count;
        }
        out.combos.push_back(std::move(at));
        out.stops.emplace_back();
        for (uint64_t k = 0; k < values->count; k++) {
            w.counterValue = values->at(k);
            for (const Exit& x : w.exits) {
                if (x.opaque || (!x.isBreak && !x.isContinue)) return std::nullopt;   // a call, a return or a disable: not judged here
                bool never = false, certain = true;
                for (const Cond& c : x.conds) {
                    V t = ev.condTruth(c);
                    const int b = truth(t);
                    if (b == 0) { never = true; break; }
                    if (b == 1) continue;
                    // a condition with an x or z bit is false in an `if`
                    if (t.k == K::Known && t.cv.isInteger()) { never = true; break; }
                    if (t.k != K::Design) return std::nullopt;   // cannot be evaluated: the loop is not judged
                    certain = false;
                }
                if (never || !certain) continue;
                out.stops.back().push_back({k, x.isContinue, x.loc});
            }
        }
    }
    w.outerValue.clear();
    return out;
}

bool alwaysJumps(const Statement& s, const std::vector<const Symbol*>& enclosing) {
    switch (s.kind) {
        case StatementKind::Break:
        case StatementKind::Continue:
        case StatementKind::Return: return true;
        case StatementKind::Disable: {
            bool ours = false;
            s.as<DisableStatement>().target.visitSymbolReferences([&](const Expression&, const Symbol& sym) {
                ours = ours || std::find(enclosing.begin(), enclosing.end(), &sym) != enclosing.end();
            });
            return ours;
        }
        case StatementKind::List:
            for (auto* c : s.as<StatementList>().list) if (alwaysJumps(*c, enclosing)) return true;
            return false;
        case StatementKind::Block: {
            const auto& b = s.as<BlockStatement>();
            return b.blockKind == StatementBlockKind::Sequential && alwaysJumps(b.body, enclosing);
        }
        default: return false;
    }
}

bool leavesEarly(const Statement& body, bool wholeProcedure) {
    Walker w;
    w.region = body.sourceRange;
    w.regionBody = &body;
    w.wholeProcedure = wholeProcedure;
    body.visit(w);
    return w.found;
}

} // namespace sverity::lint
