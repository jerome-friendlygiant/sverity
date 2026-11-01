// Native AST lint: procedural-block hygiene (LNT-015 assignment style, LNT-016 case wildcards).
#include <map>

#include "rules/rules.h"
#include "sverity/lint.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/numeric/SVInt.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;

enum class Style { Sequential, Combinational, Latch, Other };

void scanTiming(const TimingControl& t, bool& edge, bool& level) {
    switch (t.kind) {
        case TimingControlKind::SignalEvent: {
            auto& s = t.as<SignalEventControl>();
            (s.edge == EdgeKind::None ? level : edge) = true;
            break;
        }
        case TimingControlKind::EventList:
            for (auto* e : t.as<EventListControl>().events) scanTiming(*e, edge, level);
            break;
        case TimingControlKind::ImplicitEvent: level = true; break;
        default: break;
    }
}

Style styleOf(const ProceduralBlockSymbol& p) {
    switch (p.procedureKind) {
        case ProceduralBlockKind::AlwaysFF: return Style::Sequential;
        case ProceduralBlockKind::AlwaysComb: return Style::Combinational;
        case ProceduralBlockKind::AlwaysLatch: return Style::Latch;
        case ProceduralBlockKind::Always: {
            const Statement& body = p.getBody();
            if (body.kind != StatementKind::Timed) return Style::Other;
            bool edge = false, level = false;
            scanTiming(body.as<TimedStatement>().timing, edge, level);
            if (edge) return Style::Sequential;
            if (level) return Style::Combinational;
            return Style::Other;
        }
        default: return Style::Other;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// LNT-015 blocking write to state in a clocked block / LNT-062 nonblocking write in a combinational block /
// LNT-063 one variable written with both styles. One walk, three rules (each instance reports only its own kind).
class AssignStyleRule final : public LintRule {
public:
    enum Kind { BlockingInSeq, NonblockingInComb, Mixed };
    explicit AssignStyleRule(Kind k) : LintRule(k == BlockingInSeq ? "LNT-015" : k == NonblockingInComb ? "LNT-062" : "LNT-063"), kind_(k) {}

private:
    Kind kind_;
    struct Kinds { bool blocking = false, nonblocking = false; slang::SourceLocation firstLoc; const Symbol* sym = nullptr; };
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        Kind kind;
        std::map<const Symbol*, Kinds> kinds;
        V(Lint& lint, Kind k) : l(lint), kind(k) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                Style st = styleOf(p);
                Assigns c;
                p.getBody().visit(c);
                for (auto* a : c.list)
                  for (auto* sym : lvalueSymbols(a->left())) {   // a concatenation target writes every operand
                    if (!isModuleLevel(*sym)) continue;   // block-local temporaries may use either style
                    l.count();
                    auto& k = kinds[sym];
                    k.sym = sym;
                    (a->isNonBlocking() ? k.nonblocking : k.blocking) = true;
                    if (!k.firstLoc.valid()) k.firstLoc = a->sourceRange.start();
                    if (kind == BlockingInSeq && st == Style::Sequential && !a->isNonBlocking())
                        l.report(a->sourceRange.start(),
                                 "blocking assignment to '" + std::string(sym->name) + "' in a sequential block",
                                 {.nextStep = "Use '<=' for state updated on a clock edge (avoids simulation races).",
                                  .hierarchy = p.getHierarchicalPath(), .signals = {std::string(sym->name)}});
                    else if (kind == NonblockingInComb && st == Style::Combinational && a->isNonBlocking())
                        l.report(a->sourceRange.start(),
                                 "nonblocking assignment to '" + std::string(sym->name) + "' in a combinational block",
                                 {.nextStep = "Use '=' in combinational logic.",
                                  .hierarchy = p.getHierarchicalPath(), .signals = {std::string(sym->name)}});
                }
            }
            visitDefault(p);
        }
    };
    void check(Lint& l) override {
        V v(l, kind_);
        l.compilation().getRoot().visit(v);
        if (kind_ == Mixed)
            for (auto& [sym, k] : v.kinds)
                if (k.blocking && k.nonblocking)
                    l.report(k.firstLoc, "'" + std::string(sym->name) + "' is assigned with both blocking and nonblocking assignments",
                             {.nextStep = "Pick one assignment style per variable.", .hierarchy = sym->getHierarchicalPath(),
                              .signals = {std::string(sym->name)}});
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-016 casex statement / LNT-064 casez or casex item whose bits are all wildcards.
class CaseWildcardRule final : public LintRule {
public:
    enum Kind { CasexStatement, AllWildcardItem };
    explicit CaseWildcardRule(Kind k) : LintRule(k == CasexStatement ? "LNT-016" : "LNT-064"), kind_(k) {}

private:
    Kind kind_;
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        Kind kind;
        V(Lint& lint, Kind k) : l(lint), kind(k) {}
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                if (kind == CasexStatement && c.condition == CaseStatementCondition::WildcardXOrZ)
                    l.report(c.sourceRange.start(), "casex treats X and Z in the case expression as wildcards",
                             {.nextStep = "Use casez with '?' wildcards in the items, or 'case ... inside'; casex hides X propagation bugs."});
                if (kind == AllWildcardItem && c.condition != CaseStatementCondition::Normal)
                    for (auto& g : c.items)
                        for (auto* e : g.expressions)
                            if (auto cv = l.evalConst(*e); cv.isInteger()) {
                                auto& v = cv.integer();
                                bool allWild = v.getBitWidth() > 0;
                                for (uint32_t i = 0; i < v.getBitWidth() && allWild; i++) allWild = v[i].isUnknown();
                                if (allWild)
                                    l.report(e->sourceRange.start(), "case item is all wildcard bits: it matches every value and shadows later items",
                                             {.nextStep = "Use the 'default' item for a catch-all."});
                            }
            }
            visitDefault(c);
        }
    };
    void check(Lint& l) override {
        V v(l, kind_);
        l.compilation().getRoot().visit(v);
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintProceduralRules() {
    std::vector<std::unique_ptr<Rule>> v;
    for (auto k : {AssignStyleRule::BlockingInSeq, AssignStyleRule::NonblockingInComb, AssignStyleRule::Mixed})
        v.push_back(std::make_unique<AssignStyleRule>(k));
    for (auto k : {CaseWildcardRule::CasexStatement, CaseWildcardRule::AllWildcardItem})
        v.push_back(std::make_unique<CaseWildcardRule>(k));
    return v;
}

} // namespace sverity
