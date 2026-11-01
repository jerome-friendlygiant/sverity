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
// LNT-015: blocking/nonblocking policy. Sequential blocks use <= for module-level state, combinational blocks use =,
// and one variable is never assigned with both kinds.
class AssignStyleRule final : public LintRule {
public:
    AssignStyleRule() : LintRule("LNT-015") {}

private:
    struct Kinds { bool blocking = false, nonblocking = false; slang::SourceLocation firstLoc; const Symbol* sym = nullptr; };
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        std::map<const Symbol*, Kinds> kinds;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const ProceduralBlockSymbol& p) {
            if (l.isUser(p.location)) {
                Style st = styleOf(p);
                Assigns c;
                p.getBody().visit(c);
                for (auto* a : c.list) {
                    auto* sym = a->left().getSymbolReference();
                    if (!sym || !isModuleLevel(*sym)) continue;   // block-local temporaries may use either style
                    l.count();
                    auto& k = kinds[sym];
                    k.sym = sym;
                    (a->isNonBlocking() ? k.nonblocking : k.blocking) = true;
                    if (!k.firstLoc.valid()) k.firstLoc = a->sourceRange.start();
                    if (st == Style::Sequential && !a->isNonBlocking())
                        l.report(a->sourceRange.start(),
                                 "blocking assignment to '" + std::string(sym->name) + "' in a sequential block",
                                 {.nextStep = "Use '<=' for state updated on a clock edge (avoids simulation races).",
                                  .hierarchy = p.getHierarchicalPath(), .signals = {std::string(sym->name)}});
                    else if (st == Style::Combinational && a->isNonBlocking())
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
        V v(l);
        l.compilation().getRoot().visit(v);
        for (auto& [sym, k] : v.kinds)
            if (k.blocking && k.nonblocking)
                l.report(k.firstLoc, "'" + std::string(sym->name) + "' is assigned with both blocking and nonblocking assignments",
                         {.nextStep = "Pick one assignment style per variable.", .hierarchy = sym->getHierarchicalPath(),
                          .signals = {std::string(sym->name)}});
    }
};

// ---------------------------------------------------------------------------------------------------------------
// LNT-016: casex, all-wildcard items, and (via slang) missing default / incomplete case / unknown-bit items.
class CaseWildcardRule final : public LintRule {
public:
    CaseWildcardRule() : LintRule("LNT-016", "", true) {}

private:
    struct V : ASTVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        explicit V(Lint& lint) : l(lint) {}
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                if (c.condition == CaseStatementCondition::WildcardXOrZ)
                    l.report(c.sourceRange.start(), "casex treats X and Z in the case expression as wildcards",
                             {.nextStep = "Use casez with '?' wildcards in the items, or 'case ... inside'; casex hides X propagation bugs."});
                if (c.condition != CaseStatementCondition::Normal)
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
    void check(Lint& l) override { visitDesign<V>(l); }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeLintProceduralRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<AssignStyleRule>());
    v.push_back(std::make_unique<CaseWildcardRule>());
    return v;
}

} // namespace sverity
