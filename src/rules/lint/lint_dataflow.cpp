// Native lint on the flow-sensitive procedure analysis (src/analysis/flow.h): every rule here asks a question of the reaching-write
// result instead of walking statements itself.
//   LNT-013 legacy level-sensitive always blocks that leave a variable unassigned on some path (inferred latch), and task or
//           function outputs and function results some path leaves unassigned
//   LNT-053 a whole-variable write overwritten before any read
//   LNT-025 potentially conflicting conditional writes when both conditions can hold
//   LNT-044 a variable read before the procedure's first write of it
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

#include "analysis/flow.h"
#include "domains/model.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/ClassSymbols.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;
namespace fl = sverity::flow;

std::string rangeText(const ValueSymbol& s, const fl::Range& r) {
    const Type& t = s.getType();
    uint64_t w = t.isFixedSize() ? t.getBitstreamWidth() : 1;
    if (r.first == 0 && r.second + 1 == w) return std::string(s.name);
    if (r.first == r.second) return std::string(s.name) + "[" + std::to_string(r.first) + "]";
    return std::string(s.name) + "[" + std::to_string(r.second) + ":" + std::to_string(r.first) + "]";
}

// Runs `fn(proc, flow)` for every procedure of the design that lives in user code and is accepted by `pred`.
template <typename Pred, typename Fn>
struct ProcVisit : HardwareVisitor<ProcVisit<Pred, Fn>, VisitFlags::AllCanonical> {
    Lint& l;
    Pred pred;
    Fn fn;
    ProcVisit(Lint& lint, Pred p, Fn f) : l(lint), pred(std::move(p)), fn(std::move(f)) {}
    void handle(const ProceduralBlockSymbol& p) {
        if (l.isUser(p.location) && pred(p)) {
            l.count();
            fl::Flow flow(p);
            if (flow.bad)
                l.report(p.location, "procedure could not be fully analysed (slang reported an invalid statement): the result for it is incomplete",
                         {.confidence = Confidence::Unresolved, .hierarchy = p.getHierarchicalPath()});
            fn(p, flow);
        }
        this->visitDefault(p);
    }
};
template <typename Pred, typename Fn>
void forEachProc(Lint& l, Pred pred, Fn fn) {
    ProcVisit<Pred, Fn> v(l, std::move(pred), std::move(fn));
    l.visitHardware(v);
}

bool alwaysAssigns(const Statement& s, std::string_view name) {
    switch (s.kind) {
        case StatementKind::ExpressionStatement: {
            const auto& e = s.as<ExpressionStatement>().expr;
            if (e.kind != ExpressionKind::Assignment) return false;
            const auto* sym = e.as<AssignmentExpression>().left().getSymbolReference();
            return sym && sym->name == name;
        }
        case StatementKind::Block: return alwaysAssigns(s.as<BlockStatement>().body, name);
        case StatementKind::List:
            for (auto* c : s.as<StatementList>().list)
                if (alwaysAssigns(*c, name)) return true;
            return false;
        case StatementKind::Conditional: {
            const auto& c = s.as<ConditionalStatement>();
            return c.ifFalse && alwaysAssigns(c.ifTrue, name) && alwaysAssigns(*c.ifFalse, name);
        }
        case StatementKind::Case: {
            const auto& c = s.as<CaseStatement>();
            if (!c.defaultCase || !alwaysAssigns(*c.defaultCase, name)) return false;
            for (auto& item : c.items)
                if (!alwaysAssigns(*item.stmt, name)) return false;
            return true;
        }
        default: return false;
    }
}

// An assignment to `name` somewhere inside the statement.
struct WritesName : ASTVisitor<WritesName, VisitFlags::AllGood> {
    std::string_view name;
    bool hit = false;
    explicit WritesName(std::string_view n) : name(n) {}
    void handle(const AssignmentExpression& a) {
        const auto* sym = a.left().getSymbolReference();
        if (sym && sym->name == name) hit = true;
        visitDefault(a);
    }
};

// The outermost `if` or `case` of a subroutine body that assigns `name` on some paths but not on all: the statement where the
// missing assignment is, named as evidence so the site is the incomplete statement and not only the declaration.
const Statement* incompleteStatement(const Statement& s, std::string_view name) {
    switch (s.kind) {
        case StatementKind::Block: return incompleteStatement(s.as<BlockStatement>().body, name);
        case StatementKind::List:
            for (auto* c : s.as<StatementList>().list) {
                if (alwaysAssigns(*c, name)) return nullptr;
                if (const auto* hit = incompleteStatement(*c, name)) return hit;
            }
            return nullptr;
        case StatementKind::Conditional:
        case StatementKind::Case: {
            if (alwaysAssigns(s, name)) return nullptr;
            WritesName w(name);
            s.visit(w);
            return w.hit ? &s : nullptr;
        }
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-013 (subroutines): an output argument, or a function's result, that some path through the body leaves unassigned. The
// caller then receives the formal's initial value (X for a 4-state automatic variable) or, for a static subroutine, the value
// left by an earlier call: combinational logic built from it is not what the code reads as.
void checkSubroutineOutputs(Lint& l) {
    {
        struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
            Lint& l;
            // Instances of one module share a declaration; parameters can still change which paths exist, so a finding is
            // repeated only when it differs.
            std::set<std::tuple<uint32_t, size_t, std::string>> seen;
            explicit V(Lint& lint) : l(lint) {}
            void handle(const ClassType&) {}   // class methods are not design hardware (LNT-080)
            // An inactive generate branch's instances are not this configuration's hardware and are only partly bound.
            void handle(const InstanceBodySymbol& b) { if (!b.flags.has(InstanceFlags::Uninstantiated)) visitDefault(b); }
            void handle(const GenerateBlockSymbol& g) { if (!g.isUninstantiated) visitDefault(g); }
            void handle(const SubroutineSymbol& r) {
                visitDefault(r);
                if (!l.isUser(r.location) || r.flags.has(MethodFlags::DPIImport) || r.flags.has(MethodFlags::Pure) ||
                    r.flags.has(MethodFlags::InterfaceExtern) || r.flags.has(MethodFlags::ModportExport))
                    return;
                std::vector<const ValueSymbol*> outputs;
                for (auto* a : r.getArguments())
                    if (a->direction == ArgumentDirection::Out) outputs.push_back(a);
                if (r.returnValVar && !r.getReturnType().isVoid()) outputs.push_back(r.returnValVar);
                if (outputs.empty()) return;
                l.count();
                fl::Flow f(r);
                std::string names;
                std::vector<std::string> signals;
                std::vector<Evidence> evidence;
                // A loop walked once approximates only what it touches: an output no part of it reads or writes is judged exactly.
                bool approximate = f.opaque || f.bad;
                for (auto* sym : outputs) {
                    auto missing = f.unassignedAtEnd(*sym);
                    // A read that can see the formal's entry value (X, or the previous call's value) is as much a defect as a missing
                    // assignment: `f += v`, `o += v` and `f = f + v` start from it.
                    for (const auto& a : f.accesses)
                        if (!a.write && a.sym == sym && a.entryMaybe) {
                            bool covered = false;
                            for (auto& m : missing) covered = covered || (m.first <= a.range.first && a.range.second <= m.second);
                            if (!covered) missing.push_back(a.range);
                        }
                    if (missing.empty()) continue;
                    std::sort(missing.begin(), missing.end());
                    std::vector<fl::Range> merged;
                    for (auto& m : missing) {
                        if (!merged.empty() && m.first <= merged.back().second + 1) merged.back().second = std::max(merged.back().second, m.second);
                        else merged.push_back(m);
                    }
                    missing = std::move(merged);
                    const std::string what = sym == r.returnValVar ? "the result of '" + std::string(r.name) + "'" : "output '" + std::string(sym->name) + "'";
                    std::string ranges;
                    for (auto& m : missing) ranges += (ranges.empty() ? "" : ", ") + rangeText(*sym, m);
                    names += (names.empty() ? "" : "; ") + what + " (" + ranges + ")";
                    signals.push_back(std::string(sym->name));
                    if (const auto* at = incompleteStatement(r.getBody(), sym->name))
                        evidence.push_back({"incomplete_statement", std::string("this ") + (at->kind == StatementKind::Case ? "case" : "if") + " leaves '" + std::string(sym->name) + "' unassigned on some path", l.loc(at->sourceRange.start())});
                    approximate = approximate || ((f.truncated || f.walkedOnce) && f.touchedInWalkedLoop(*sym));
                }
                const std::string kind = r.subroutineKind == SubroutineKind::Task ? "task" : "function";
                const bool unproven = names.empty() && (f.pathsDropped || f.bad);
                if ((!names.empty() || unproven) &&
                    !seen.insert({r.location.buffer().getId(), r.location.offset(), names + (unproven ? "?" : "")}).second)
                    return;
                if (!names.empty())
                    l.report(r.location, kind + " '" + std::string(r.name) + "' does not assign " + names +
                                             " on every path: the caller receives the formal's initial or earlier value",
                             {.confidence = approximate ? Confidence::Unresolved : Confidence::Established,
                              .nextStep = "Assign every output and the result at the top of the body, or cover the missing paths.",
                              .signals = signals, .evidence = evidence});
                else if (f.pathsDropped || f.bad)
                    l.report(r.location, kind + " '" + std::string(r.name) + "': assignment of every output on every path cannot be proven (" +
                                             (f.bad ? "slang could not analyse the body" : f.truncated && !f.pathsDropped ? "a loop is not unrolled (its count is not constant, or it is longer than the unrolling budget)" : "a continue or a case with items that cannot match approximates the path analysis") + ")",
                             {.confidence = Confidence::Unresolved,
                              .nextStep = "Review the control flow and ensure every output and the result is assigned on every path."});
            }
        };
        V v(l);
        l.visitHardware(v);   // canonical bodies: one finding per subroutine declaration
    }
}


// ---------------------------------------------------------------------------------------------------------------
// LNT-013 (static state): a static variable declared inside a combinational procedure, or inside a subroutine one calls, that a
// path reads before it is written keeps its value from one activation to the next: it is storage in logic that reads as
// combinational. (An automatic local read before it is written is X, which LNT-044 covers.)
void checkStaticLocals(Lint& l) {
    std::set<std::tuple<uint32_t, size_t, std::string>> seen;
    std::set<const SubroutineSymbol*> called;
    std::vector<const SubroutineSymbol*> work;
    auto check = [&](const fl::Flow& f, const Symbol& owner, const std::string& hierarchy, const SubroutineSymbol* routine) {
        std::set<const ValueSymbol*> done;
        for (const auto& a : f.accesses) {
            if (a.write || !a.local || !a.entryMaybe || done.count(a.sym)) continue;
            const auto* v = a.sym->as_if<VariableSymbol>();
            if (!v || v->kind != SymbolKind::Variable || v->lifetime != VariableLifetime::Static) continue;
            if (routine && v == routine->returnValVar) continue;
            done.insert(a.sym);
            if (!seen.insert({owner.location.buffer().getId(), a.loc.offset(), std::string(v->name)}).second) continue;
            l.report(a.loc, "static variable '" + std::string(v->name) + "' is read before it is written on some path: it keeps its value between activations, "
                            "so this combinational logic holds state",
                     {.confidence = f.pathsDropped || f.bad || !a.entryHard ? Confidence::Unresolved : Confidence::Established,
                      .nextStep = "Declare the variable automatic, or assign it before every read.",
                      .hierarchy = hierarchy});
        }
    };
    struct Calls : ASTVisitor<Calls, VisitFlags::AllGood> {
        std::vector<const SubroutineSymbol*> found;
        void handle(const CallExpression& c) {
            if (!c.isSystemCall())
                if (const auto* sub = std::get_if<const SubroutineSymbol*>(&c.subroutine); sub && *sub) found.push_back(*sub);
            visitDefault(c);
        }
    };
    // Combinational and latch procedures (a latch holds state by design, but not through a hidden variable), and the continuous
    // assignments, whose calls re-run on every change of an operand.
    auto comb = [](const ProceduralBlockSymbol& p) {
        const auto st = fl::styleOf(p);
        return st == fl::Style::Comb || st == fl::Style::Latch || st == fl::Style::Level;
    };
    forEachProc(l, comb, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
        check(f, p, p.getHierarchicalPath(), nullptr);
        Calls c;
        p.getBody().visit(c);
        for (auto* sub : c.found) if (called.insert(sub).second) work.push_back(sub);
    });
    struct Assigns : HardwareVisitor<Assigns, VisitFlags::AllCanonical> {
        Lint& l;
        Calls& calls;
        Assigns(Lint& lint, Calls& c) : l(lint), calls(c) {}
        void handle(const ContinuousAssignSymbol& ca) {
            if (l.isUser(ca.location)) ca.getAssignment().visit(calls);
            visitDefault(ca);
        }
        // `wire w = f(a);` is a continuous assignment, and a port connection expression (`.i(f(a))`) re-evaluates on every change too.
        void handle(const NetSymbol& n) {
            if (l.isUser(n.location))
                if (const auto* init = n.getInitializer()) init->visit(calls);
            visitDefault(n);
        }
        void handle(const InstanceSymbol& inst) {
            if (l.isUser(inst.location))
                for (const auto* pc : inst.getPortConnections())
                    if (const Expression* ex = pc->getExpression()) ex->visit(calls);
            visitDefault(inst);
        }
        void handle(const PrimitiveInstanceSymbol& prim) {
            if (l.isUser(prim.location))
                for (const auto* ex : prim.getPortConnections()) ex->visit(calls);
            visitDefault(prim);
        }
    };
    {
        Calls c;
        Assigns a(l, c);
        l.visitHardware(a);
        for (auto* sub : c.found) if (called.insert(sub).second) work.push_back(sub);
    }
    while (!work.empty()) {
        const SubroutineSymbol* sub = work.back();
        work.pop_back();
        if (!l.isUser(sub->location) || sub->flags.has(MethodFlags::DPIImport) || sub->flags.has(MethodFlags::Pure) || sub->flags.has(MethodFlags::InterfaceExtern)) continue;
        fl::Flow f(*sub);
        check(f, *sub, std::string(sub->name), sub);
        Calls c;
        sub->getBody().visit(c);
        for (auto* next : c.found) if (called.insert(next).second) work.push_back(next);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-013 (legacy blocks): a module-level variable whose final value can still be the one from before the block.
class LegacyLatchRule final : public LintRule {
public:
    LegacyLatchRule() : LintRule("LNT-013", "", true) {}
    // slang locates its inferred-latch finding at an assignment inside the incomplete statement. The statement that leaves the signal
    // unassigned on some path (the outermost `if` or `case` around that assignment) is named as evidence, so the site is where the
    // defect is: the statement's own header.
    static void nameIncompleteStatements(Lint& l) {
        std::vector<Diagnostic*> latches;
        for (auto& d : l.context().report.mutableDiagnostics())
            if (d.ruleId == "LNT-013" && d.origin == "slang" && d.upstreamCode == "inferred-latch" && !d.signals.empty() && d.location.line) latches.push_back(&d);
        if (latches.empty()) return;
        struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
            Lint& l;
            Diagnostic& d;
            bool done = false;
            Find(Lint& lint, Diagnostic& diag) : l(lint), d(diag) {}
            // true when the statement was judged here (named, or none of its parts matters); false: look inside it
            bool consider(const Statement& s, bool isCase) {
                if (done) return true;
                const auto from = l.loc(s.sourceRange.start()), to = l.loc(s.sourceRange.end());
                if (from.file != d.location.file || d.location.line < from.line || d.location.line > to.line) return false;
                if (alwaysAssigns(s, d.signals.front())) return false;
                d.evidence.push_back({"incomplete_statement", std::string("this ") + (isCase ? "case" : "if") + " leaves '" + d.signals.front() + "' unassigned on some path", from});
                done = true;
                return true;
            }
            void handle(const CaseStatement& s) { if (!consider(s, true)) visitDefault(s); }
            void handle(const ConditionalStatement& s) { if (!consider(s, false)) visitDefault(s); }
        };
        struct Procs : HardwareVisitor<Procs, VisitFlags::AllCanonical> {
            Lint& l;
            std::vector<Diagnostic*>& latches;
            Procs(Lint& lint, std::vector<Diagnostic*>& all) : l(lint), latches(all) {}
            void handle(const ProceduralBlockSymbol& p) {
                const auto from = l.loc(p.getBody().sourceRange.start()), to = l.loc(p.getBody().sourceRange.end());
                for (auto* d : latches)
                    if (d->location.file == from.file && d->location.line >= from.line && d->location.line <= to.line) {
                        Find find(l, *d);
                        p.getBody().visit(find);
                    }
                visitDefault(p);
            }
        } procs(l, latches);
        l.visitHardware(procs);
    }
    // slang takes a `unique`/`priority` case with no default for complete, but a default item that is written and leaves a variable
    // unassigned is a path the author spelled out: values reach it (an enum has encodings and values the items do not name), and the
    // variable keeps its old value there. The `unique` claim is no promise that the path is dead for the latch; it only makes the finding an
    // obligation (Unresolved) when every enumerator has an item and the default can only be taken by an encoding no enumerator has.
    static void defaultItemLatches(Lint& l) {
        auto combWithQualifiedCase = [](const ProceduralBlockSymbol& p) {
            if (p.procedureKind != ProceduralBlockKind::AlwaysComb) return false;
            struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
                bool found = false;
                void handle(const CaseStatement& c) {
                    found = found || (c.check != UniquePriorityCheck::None && c.defaultCase);
                    visitDefault(c);
                }
            } f;
            p.getBody().visit(f);
            return f.found;
        };
        forEachProc(l, combWithQualifiedCase, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            if (f.opaque || f.truncated || f.pathsDropped) return;
            const CaseStatement* where = nullptr;
            bool enumCovered = false;
            struct Find : ASTVisitor<Find, VisitFlags::AllGood> {
                const CaseStatement* found = nullptr;
                void handle(const CaseStatement& c) {
                    if (!found && c.check != UniquePriorityCheck::None && c.defaultCase) found = &c;
                    visitDefault(c);
                }
            } find;
            p.getBody().visit(find);
            where = find.found;
            if (!where) return;
            // The bits this case itself writes: a variable the case never assigns is not left unassigned by its default item, whatever a
            // later statement of the block does to it (that latch belongs to the ordinary finding).
            const auto within = [&](slang::SourceLocation at) {
                if (!at.valid()) return false;
                const auto from = l.sm().getFullyExpandedLoc(where->sourceRange.start()), to = l.sm().getFullyExpandedLoc(where->sourceRange.end());
                const auto here = l.sm().getFullyExpandedLoc(at);
                return here.buffer().getId() == from.buffer().getId() && here.offset() >= from.offset() && here.offset() <= to.offset();
            };
            std::string names;
            for (auto* sym : f.writtenSymbols()) {
                bool local = true;
                for (auto& a : f.accesses) if (a.sym == sym && a.write) local = a.local;
                if (local) continue;
                // only the bits this case writes: the other fields of a struct are written by other procedures, other bits by other statements
                std::vector<fl::Range> mine;
                for (auto& a : f.accesses) if (a.sym == sym && a.write && !a.local && within(a.loc)) mine.push_back(a.range);
                for (auto& r : f.unassignedAtEnd(*sym)) {
                    fl::Range clipped{0, 0};
                    bool any = false;
                    for (auto& w : mine) {
                        const uint64_t lo = std::max(r.first, w.first), hi = std::min(r.second, w.second);
                        if (lo <= hi) { clipped = any ? fl::Range{std::min(clipped.first, lo), std::max(clipped.second, hi)} : fl::Range{lo, hi}; any = true; }
                    }
                    if (any) names += (names.empty() ? "" : ", ") + rangeText(*sym, clipped);
                }
            }
            if (names.empty()) return;
            const auto& type = where->expr.type->getCanonicalType();
            if (type.isEnum()) {
                std::set<uint64_t> listed;
                for (auto& item : where->items)
                    for (auto* e : item.expressions) {
                        auto v = l.evalConst(*e);
                        if (v.isInteger() && !v.integer().hasUnknown())
                            if (auto n = v.integer().as<uint64_t>()) listed.insert(*n);
                    }
                enumCovered = true;
                for (auto& ev : type.as<EnumType>().values()) {
                    auto v = ev.getValue();
                    auto n = v.isInteger() && !v.integer().hasUnknown() ? v.integer().as<uint64_t>() : std::nullopt;
                    if (!n || !listed.count(*n)) enumCovered = false;
                }
            }
            l.report(where->defaultCase->sourceRange.start(),
                     "latch inferred for [" + names + "]: the default item of this " + (where->check == UniquePriorityCheck::Priority ? "priority" : "unique") +
                         " case assigns nothing, so the old value is kept on that path" +
                         (enumCovered ? " (every enumerator has an item, so only an encoding no enumerator names reaches it)" : ""),
                     {.confidence = enumCovered ? Confidence::Unresolved : Confidence::Established,
                      .nextStep = "Assign the variable in the default item (or before the case); `unique` does not make the default dead for storage.",
                      .hierarchy = p.getHierarchicalPath(), .signals = {names}});
        });
    }
    void check(Lint& l) override {
        defaultItemLatches(l);
        nameIncompleteStatements(l);
        checkSubroutineOutputs(l);
        checkStaticLocals(l);
        auto legacy = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return p.procedureKind == ProceduralBlockKind::Always && (st == fl::Style::Comb || st == fl::Style::Level);
        };
        forEachProc(l, legacy, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            std::string names;
            for (auto* sym : f.writtenSymbols()) {
                bool local = true;
                for (auto& a : f.accesses) if (a.sym == sym && a.write) local = a.local;
                if (local) continue;
                for (auto& r : f.unassignedAtEnd(*sym)) names += (names.empty() ? "" : ", ") + rangeText(*sym, r);
            }
            if (names.empty() && (f.opaque || f.pathsDropped))
                l.report(p.location, std::string("level-sensitive always block: assignment on every path cannot be proven (") +
                                         (f.opaque ? "it calls a user subroutine or has a timing control" : f.truncated && !f.pathsDropped ? "a loop is not unrolled (its count is not constant, or it is longer than the unrolling budget)" : "a continue or a case with items that cannot match approximates the path analysis") + ")",
                         {.confidence = Confidence::Unresolved, .nextStep = "Review unsupported control flow and ensure every combinational output is assigned on every path; always_comb alone does not remove storage.", .hierarchy = p.getHierarchicalPath()});
            if (!names.empty())
                l.report(p.location, "level-sensitive always block does not assign [" + names + "] on every path: a latch is inferred",
                         {.confidence = f.opaque || f.truncated ? Confidence::Unresolved : Confidence::Heuristic,
                          .nextStep = "Assign a default at the top of the block or cover missing else/default paths; always_comb can diagnose storage but does not repair it.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {names}});
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-053: a whole-variable write replaced by a later whole-variable write that always runs when the first one did, with no
// read in between. Same path guards = Established; the overwrite sits in an enclosing list (the first write is conditional) =
// Heuristic "on at least one path". A user call between the two writes may read the value: Unresolved. A timing control
// between them lets other processes see the first value: no finding. The default-then-override idiom (`z = 0; if (c) z = a;`)
// does not qualify: the overwrite is conditional, so the default stays live on the other path.
class DeadStoreRule final : public LintRule {
public:
    DeadStoreRule() : LintRule("LNT-053") {}
    void check(Lint& l) override {
        forEachProc(l, [](const ProceduralBlockSymbol& p) { return p.procedureKind != ProceduralBlockKind::Final; },
                    [&](const ProceduralBlockSymbol&, const fl::Flow& f) {
            std::set<uint32_t> reported;
            for (auto& k : f.kills) {
                const fl::Access &v = f.accesses[k.victim], &w = f.accesses[k.killer];
                if (!v.exact || !w.exact || v.compound || v.nonblocking != w.nonblocking) continue;
                if (v.inLoop || w.inLoop || k.readBefore || k.observed) continue;
                if (!fl::Flow::guardImplies(v, w)) continue;   // the overwrite is conditional relative to the first write
                if (!reported.insert(k.victim).second) continue;
                bool same = fl::Flow::guardImplies(w, v);
                l.report(v.loc, "assignment to '" + rangeText(*v.sym, v.range) + "' is overwritten before it is read" + (same ? "" : " (on at least one path)"),
                         {.confidence = k.exposed ? Confidence::Unresolved : (same ? Confidence::Established : Confidence::Heuristic),
                          .nextStep = k.exposed ? "A subroutine call lies between the two writes and may read the value; review it." : "Remove the first assignment or use the value.",
                          .signals = {std::string(v.sym->name)}});
            }
        });
    }
};

// Equal literal writes to the same integral range cannot disagree on the final value.
// Evaluate the bound RHS including assignment conversions, not its source spelling.
// Keep opaque calls and different ranges out: neither is an equality proof.
bool sameLiteralValue(const Lint& l, const fl::Access& v, const fl::Access& w) {
    if (!v.asg || !w.asg || v.compound || w.compound || v.range != w.range) return false;
    const auto& a = v.asg->right();
    const auto& b = w.asg->right();
    auto literal = [](const Expression& e) {
        auto kind = e.unwrapImplicitConversions().kind;
        return kind == ExpressionKind::IntegerLiteral || kind == ExpressionKind::UnbasedUnsizedIntegerLiteral;
    };
    if (!literal(a) || !literal(b)) return false;
    auto av = l.evalConst(a), bv = l.evalConst(b);
    return av.isInteger() && bv.isInteger() && !av.integer().hasUnknown() && !bv.integer().hasUnknown() &&
           av.integer().getBitWidth() == bv.integer().getBitWidth() && exactlyEqual(av.integer(), bv.integer());
}

// This is an advisory recognition of text selection, not an exclusivity proof.
// Restrict it to whole packed string-literal writes; numeric state/control chains
// need their own policy. Every conditional write must participate, so an unrelated
// text write cannot lower a mixed data/control conflict.
bool textPriorityChain(const Lint& l, const fl::Flow& f, const fl::Access& target,
                       const std::set<const ValueSymbol*>& controls) {
    if (f.bad || f.opaque || f.approximated || controls.count(target.sym)) return false;
    if (!target.sym->getType().isIntegral() || target.sym->getType().getBitstreamWidth() <= 8) return false;
    std::vector<const fl::Access*> writes;
    for (const auto& a : f.accesses) {
        if (a.inLoop) return false;
        if (!a.write || a.sym != target.sym) continue;
        if (!a.whole || !a.exact || a.compound || !a.asg || a.nonblocking != target.nonblocking) return false;
        const auto& rhs = a.asg->right();
        if (rhs.unwrapImplicitConversions().kind != ExpressionKind::StringLiteral) return false;
        auto value = l.evalConst(rhs);
        if (!value.isInteger() || value.integer().hasUnknown()) return false;
        if (!a.guards->empty()) writes.push_back(&a);
    }
    if (writes.size() < 3) return false;
    for (size_t i = 0; i < writes.size(); ++i)
        for (size_t j = i + 1; j < writes.size(); ++j)
            if (fl::Flow::guardImplies(*writes[i], *writes[j]) ||
                fl::Flow::guardImplies(*writes[j], *writes[i]) || f.exclusive(*writes[i], *writes[j])) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-025: an earlier conditional whole-variable write is overridden by a later conditional one, with no read in between, when the
// conditions are independent and not provably exclusive (different branches of one if/case, or comparisons of one operand with
// different constants). Blocking writes in combinational procedures and nonblocking writes in clocked procedures; the procedure
// kind is reported as evidence.
class OverrideRule final : public LintRule {
public:
    OverrideRule() : LintRule("LNT-025") {}
    void check(Lint& l) override {
        auto want = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return st == fl::Style::Seq || st == fl::Style::Comb || st == fl::Style::Latch || st == fl::Style::Level;
        };
        // Exclude all modelled control sources, including their upstream aliases.
        auto& m = model::getModel(l.context().design);
        std::vector<std::vector<model::NodeId>> upstream(m.nodes.size());
        std::vector<model::NodeId> pending;
        for (const auto& e : m.edges) {
            upstream[e.to].push_back(e.from);
            if (e.ctrl) pending.push_back(e.from);
        }
        for (const auto& flop : m.flops) {
            if (flop.clockSignal != model::kNoNode) pending.push_back(flop.clockSignal);
            for (const auto& reset : flop.resets) pending.push_back(reset.signal);
        }
        std::set<model::NodeId> visited;
        std::set<const ValueSymbol*> controls;
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            if (node == model::kNoNode || !visited.insert(node).second) continue;
            controls.insert(m.nodes[node].sym);
            for (auto from : upstream[node]) pending.push_back(from);
        }
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            uint32_t procedureOrdinal = 0;
            std::string unit;
            if (const auto* scope = p.getParentScope(); scope && scope->asSymbol().kind == SymbolKind::InstanceBody) {
                const auto& definition = scope->asSymbol().as<InstanceBodySymbol>().getDefinition();
                unit = std::to_string(definition.sourceLibrary.name.size()) + ":" + definition.sourceLibrary.name + std::string(definition.name);
                uint32_t ordinal = 0;
                for (const auto& member : scope->members()) {
                    if (member.kind == slang::ast::SymbolKind::ProceduralBlock) ++ordinal;
                    if (&member == &p) { procedureOrdinal = ordinal; break; }
                }
            }
            for (auto& k : f.kills) {
                const fl::Access &v = f.accesses[k.victim], &w = f.accesses[k.killer];
                if (!v.exact || !w.exact || v.compound || v.nonblocking != seq || w.nonblocking != seq) continue;
                if (v.guards->empty() || w.guards->empty() || v.inLoop || w.inLoop) continue;
                if (k.readBefore || k.observed) continue;
                if (fl::Flow::guardImplies(v, w) || fl::Flow::guardImplies(w, v)) continue;   // nested or dominating: LNT-053 / intended priority
                if (f.exclusive(v, w)) continue;
                if (!k.exposed && sameLiteralValue(l, v, w)) continue;
                bool advisory = !k.exposed && textPriorityChain(l, f, v, controls);
                bool unresolved = k.exposed || f.opaque || f.approximated || f.bad;
                l.report(w.loc, "potentially conflicting conditional assignment to '" + rangeText(*v.sym, w.range) + "' overrides the one on line " +
                                    std::to_string(l.sm().getLineNumber(v.loc)) + " if both conditions hold (overlap and intent are not proven)" +
                                    (k.exposed ? " (a subroutine call between them may have read the value)" : "") +
                                    (advisory ? " (string-literal chain assumed to be intended priority; exclusivity is not proved)" : ""),
                         // Policy: one procedure's later conditional write is source-ordered priority, usually deliberate
                         // (OpenTitan triage: ten of ten sites). Advisory unless LNT-025.overrides=warning. A pair the
                         // analysis cannot decide (Unresolved) is never advisory: unknown is not safe.
                         {.severity = advisory ? (l.option("priority_chains", "info") != "warning" ? Severity::Info : Severity::Warning)
                                               : (unresolved || l.option("overrides", "info") == "warning" ? Severity::Warning : Severity::Info),
                          .confidence = unresolved ? Confidence::Unresolved : Confidence::Heuristic,
                          .proof = ProofState::NotApplicable,
                          .nextStep = advisory ? "Review the assumed text-selection priority; use --severity LNT-025=WARNING or --option LNT-025.priority_chains=warning to raise it."
                                               : "Review the priority or prove the conditions exclusive; independent if statements may intentionally encode last-write priority.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {std::string(v.sym->name)},
                          .evidence = {{"procedure", seq ? "clocked (nonblocking writes)" : "combinational (blocking writes)", {}}},
                          .groupingUnit = unit, .groupingProcedureOrdinal = procedureOrdinal,
                          .identityKey = std::to_string(l.sm().getLineNumber(v.loc)) + ":" + std::to_string(l.sm().getColumnNumber(v.loc)) +
                              ":" + rangeText(*v.sym, v.range) + ":" + rangeText(*v.sym, w.range)});
            }
        });
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-044: a module-level variable read while every path still holds the value from before the procedure, followed later in the
// procedure by a whole-variable write. Combinational procedures: any write counts (x = x + 1 reads before it assigns).
// Clocked procedures: only blocking writes count (a nonblocking read sees the old value by design) and the write must belong to
// another assignment (cnt = cnt + 1 is the register idiom).
class ReadBeforeAssignRule final : public LintRule {
public:
    ReadBeforeAssignRule() : LintRule("LNT-044") {}
    // An automatic variable of a block or subroutine that some path reads before writing it: its value is undefined (X) on that path,
    // not the earlier activation's (a static one is LNT-013's). A loop counter declared by its `for` header starts at its initialiser.
    static void checkAutomaticLocals(Lint& l, const fl::Flow& f, const Symbol& owner, const std::string& hierarchy, const SubroutineSymbol* routine,
                                     std::set<std::tuple<uint32_t, size_t, std::string>>& seen) {
        struct Headers : ASTVisitor<Headers, VisitFlags::AllGood> {
            std::set<const Symbol*> counters;
            void handle(const ForLoopStatement& loop) {
                for (const auto* v : loop.loopVars) counters.insert(v);
                visitDefault(loop);
            }
        } headers;
        if (routine) routine->getBody().visit(headers);
        else if (const auto* block = owner.as_if<ProceduralBlockSymbol>()) block->getBody().visit(headers);
        std::set<const ValueSymbol*> done;
        for (const auto& a : f.accesses) {
            if (a.write || !a.local || !a.entryMaybe || done.count(a.sym)) continue;
            const auto* v = a.sym->as_if<VariableSymbol>();
            if (!v || v->kind != SymbolKind::Variable || v->lifetime != VariableLifetime::Automatic || headers.counters.count(v)) continue;
            if (routine && v == routine->returnValVar) continue;
            done.insert(a.sym);
            if (!seen.insert({owner.location.buffer().getId(), a.loc.offset(), std::string(v->name)}).second) continue;
            l.report(a.loc, "automatic variable '" + std::string(v->name) + "' is read before it is written on some path: its value there is undefined",
                     {.confidence = f.pathsDropped || f.bad || !a.entryHard ? Confidence::Unresolved : Confidence::Established,
                      .nextStep = "Assign the variable before every read.",
                      .hierarchy = hierarchy, .signals = {std::string(v->name)}});
        }
    }
    void check(Lint& l) override {
        std::set<std::tuple<uint32_t, size_t, std::string>> seenLocals;
        {
            struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
                Lint& l;
                std::set<std::tuple<uint32_t, size_t, std::string>>& seen;
                V(Lint& lint, std::set<std::tuple<uint32_t, size_t, std::string>>& s) : l(lint), seen(s) {}
                void handle(const ClassType&) {}   // class methods are not design hardware (LNT-080)
                void handle(const InstanceBodySymbol& b) { if (!b.flags.has(InstanceFlags::Uninstantiated)) visitDefault(b); }
                void handle(const GenerateBlockSymbol& g) { if (!g.isUninstantiated) visitDefault(g); }
                void handle(const SubroutineSymbol& r) {
                    visitDefault(r);
                    if (!l.isUser(r.location) || r.flags.has(MethodFlags::DPIImport) || r.flags.has(MethodFlags::Pure) ||
                        r.flags.has(MethodFlags::InterfaceExtern) || r.flags.has(MethodFlags::ModportExport))
                        return;
                    l.count();
                    fl::Flow f(r);
                    checkAutomaticLocals(l, f, r, std::string(r.name), &r, seen);
                }
            } v(l, seenLocals);
            l.visitHardware(v);
        }
        auto want = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return st == fl::Style::Comb || st == fl::Style::Seq;
        };
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            checkAutomaticLocals(l, f, p, p.getHierarchicalPath(), nullptr, seenLocals);
            std::set<const ValueSymbol*> done;
            // The non-compound writes of each symbol, in order: a read looks only at those after it (a long unrolled loop has thousands of
            // accesses, and a scan of all of them per read is quadratic).
            std::map<const ValueSymbol*, std::vector<uint32_t>> laterWrites;
            for (uint32_t j = 0; j < f.accesses.size(); j++) {
                const fl::Access& w = f.accesses[j];
                if (w.write && !w.compound) laterWrites[w.sym].push_back(j);
            }
            for (uint32_t i = 0; i < f.accesses.size(); i++) {
                const fl::Access& r = f.accesses[i];
                if (r.write || r.local || !r.entryMaybe || done.count(r.sym)) continue;
                auto list = laterWrites.find(r.sym);
                if (list == laterWrites.end()) continue;
                if (!r.entryOnly) {
                    // A read that sees some of its bits at the value from before the block and others written earlier (or filled by a write
                    // the walk cannot place): the bits a later write of the block then covers are read before they are written.
                    std::vector<fl::Range> sole, some, unsure;
                    for (const auto& seg : r.entrySegs) {
                        for (auto it = std::upper_bound(list->second.begin(), list->second.end(), i); it != list->second.end(); ++it) {
                            const fl::Access& w = f.accesses[*it];
                            if (seq && (w.nonblocking || w.asg == r.asg)) continue;
                            const uint64_t lo = std::max(seg.range.first, w.range.first), hi = std::min(seg.range.second, w.range.second);
                            if (lo > hi) continue;
                            const bool exact = w.whole || w.exact;
                            (seg.kind == fl::Access::Entry::Unsure || !exact ? unsure : seg.kind == fl::Access::Entry::Sole ? sole : some)
                                .push_back(fl::Range{lo, hi});
                        }
                    }
                    const auto text = [&](std::vector<fl::Range> v) {
                        std::sort(v.begin(), v.end());
                        std::vector<fl::Range> merged;
                        for (const auto& x : v) {
                            if (!merged.empty() && x.first <= merged.back().second + 1) merged.back().second = std::max(merged.back().second, x.second);
                            else merged.push_back(x);
                        }
                        std::string out;
                        for (const auto& x : merged) out += (out.empty() ? "" : ", ") + rangeText(*r.sym, x);
                        return out;
                    };
                    if (sole.empty() && some.empty() && unsure.empty()) continue;
                    done.insert(r.sym);
                    if (!sole.empty() || !some.empty()) {
                        const bool onlySome = sole.empty();
                        sole.insert(sole.end(), some.begin(), some.end());
                        l.report(r.loc, "'" + std::string(r.sym->name) + "' is read before " +
                                            (onlySome ? "it is written on some path of this block, for [" + text(sole) + "]: that path sees the previous value"
                                                      : "its first assignment in this block, for [" + text(sole) + "]: the read sees the previous value of those bits"),
                                 {.confidence = f.approximated || f.truncated ? Confidence::Unresolved : onlySome ? Confidence::Heuristic : Confidence::Established,
                                  .nextStep = "Assign the bits before reading them, or read them through a separate variable.",
                                  .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                    } else {
                        l.report(r.loc, "'" + std::string(r.sym->name) + "' is read at its value from before this block for [" + text(unsure) + "] unless a write through an index this analysis cannot place has filled it, and is written later: whether the read sees the previous value is not decided",
                                 {.confidence = Confidence::Unresolved,
                                  .nextStep = "Assign the bits before reading them, or read them through a separate variable.",
                                  .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                    }
                    continue;
                }
                const fl::Access* maybe = nullptr;   // a later write that may reach the bits read, without covering them for certain
                for (auto it = std::upper_bound(list->second.begin(), list->second.end(), i); it != list->second.end(); ++it) {
                    const fl::Access& w = f.accesses[*it];
                    if (seq && (w.nonblocking || w.asg == r.asg)) continue;
                    // The write covers the bits read: the whole variable, or the exact bits of a static select (an unrolled loop's too).
                    const bool covers = w.whole || (w.exact && w.range.first <= r.range.first && w.range.second >= r.range.second);
                    if (!covers) {
                        if (!maybe && w.range.first <= r.range.second && r.range.first <= w.range.second) maybe = &w;
                        continue;
                    }
                    done.insert(r.sym);
                    l.report(r.loc, "'" + std::string(r.sym->name) + "' is read before its first assignment in this block: the read sees the previous value",
                             {.confidence = f.approximated || f.truncated ? Confidence::Unresolved : Confidence::Established,
                              .nextStep = "Assign the variable before reading it, or read it through a separate variable.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                    maybe = nullptr;
                    break;
                }
                if (maybe && !done.count(r.sym)) {
                    done.insert(r.sym);
                    l.report(r.loc, "'" + std::string(r.sym->name) + "' is read at its value from before this block and written later through a select this analysis cannot place: whether the read sees the previous value is not decided",
                             {.confidence = Confidence::Unresolved,
                              .nextStep = "Assign the bits before reading them, or read them through a separate variable.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                }
            }
        });
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintDataflowRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<LegacyLatchRule>());
    v.push_back(std::make_unique<DeadStoreRule>());
    v.push_back(std::make_unique<OverrideRule>());
    v.push_back(std::make_unique<ReadBeforeAssignRule>());
    return v;
}

} // namespace sverity
