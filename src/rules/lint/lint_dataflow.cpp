// Native lint on the flow-sensitive procedure analysis (src/analysis/flow.h): every rule here asks a question of the reaching-write
// result instead of walking statements itself.
//   LNT-013 legacy level-sensitive always blocks that leave a variable unassigned on some path (inferred latch)
//   LNT-053 a whole-variable write overwritten before any read
//   LNT-025 a conditional write overridden by a later conditional write when both conditions can hold
//   LNT-044 a variable read before the procedure's first write of it
#include <set>

#include "analysis/flow.h"
#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

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
struct ProcVisit : ASTVisitor<ProcVisit<Pred, Fn>, VisitFlags::AllCanonical> {
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
    l.compilation().getRoot().visit(v);
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-013 (legacy blocks): a module-level variable whose final value can still be the one from before the block.
class LegacyLatchRule final : public LintRule {
public:
    LegacyLatchRule() : LintRule("LNT-013", "", true) {}
    void check(Lint& l) override {
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
            if (names.empty() && (f.opaque || f.approximated))
                l.report(p.location, std::string("level-sensitive always block: assignment on every path cannot be proven (") +
                                         (f.opaque ? "it calls a user subroutine or has a timing control" : "a continue or a case with items that cannot match approximates the path analysis") + ")",
                         {.confidence = Confidence::Unresolved, .nextStep = "Assign a default at the top of the block, or use always_comb.", .hierarchy = p.getHierarchicalPath()});
            if (!names.empty())
                l.report(p.location, "level-sensitive always block does not assign [" + names + "] on every path: a latch is inferred",
                         {.confidence = f.opaque ? Confidence::Unresolved : Confidence::Heuristic,
                          .nextStep = "Assign a default at the top of the block, add the else/default branch, or use always_comb to get slang's check.",
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
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            for (auto& k : f.kills) {
                const fl::Access &v = f.accesses[k.victim], &w = f.accesses[k.killer];
                if (!v.exact || !w.exact || v.compound || v.nonblocking != seq || w.nonblocking != seq) continue;
                if (v.guards->empty() || w.guards->empty() || v.inLoop || w.inLoop) continue;
                if (k.readBefore || k.observed) continue;
                if (fl::Flow::guardImplies(v, w) || fl::Flow::guardImplies(w, v)) continue;   // nested or dominating: LNT-053 / intended priority
                if (f.exclusive(v, w)) continue;
                l.report(w.loc, "conditional assignment to '" + rangeText(*v.sym, w.range) + "' overrides the one on line " +
                                    std::to_string(l.sm().getLineNumber(v.loc)) + " when both conditions hold" +
                                    (k.exposed ? " (a subroutine call between them may have read the value)" : ""),
                         {.confidence = k.exposed ? Confidence::Unresolved : Confidence::Heuristic,
                          .nextStep = "Use if/else or a priority case so the intended choice is explicit; the conditions may be mutually exclusive.",
                          .hierarchy = p.getHierarchicalPath(), .signals = {std::string(v.sym->name)},
                          .evidence = {{"procedure", seq ? "clocked (nonblocking writes)" : "combinational (blocking writes)", {}}}});
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
    void check(Lint& l) override {
        auto want = [](const ProceduralBlockSymbol& p) {
            auto st = fl::styleOf(p);
            return st == fl::Style::Comb || st == fl::Style::Seq;
        };
        forEachProc(l, want, [&](const ProceduralBlockSymbol& p, const fl::Flow& f) {
            bool seq = fl::styleOf(p) == fl::Style::Seq;
            std::set<const ValueSymbol*> done;
            for (uint32_t i = 0; i < f.accesses.size(); i++) {
                const fl::Access& r = f.accesses[i];
                if (r.write || r.local || !r.entryOnly || done.count(r.sym)) continue;
                for (uint32_t j = i + 1; j < f.accesses.size(); j++) {
                    const fl::Access& w = f.accesses[j];
                    if (!w.write || w.sym != r.sym || !w.whole || w.compound) continue;
                    if (seq && (w.nonblocking || w.asg == r.asg)) continue;
                    done.insert(r.sym);
                    l.report(r.loc, "'" + std::string(r.sym->name) + "' is read before its first assignment in this block: the read sees the previous value",
                             {.nextStep = "Assign the variable before reading it, or read it through a separate variable.",
                              .hierarchy = p.getHierarchicalPath(), .signals = {std::string(r.sym->name)}});
                    break;
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
