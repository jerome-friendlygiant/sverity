// Native AST lint: procedural-block hygiene (LNT-015 assignment style, LNT-016 case wildcards).
#include <map>

#include "rules/rules.h"
#include "domains/model.h"
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
    void check(Lint& l) override {
        struct Seen { bool blocking = false, nonblocking = false; SourceLocation loc; };
        std::map<const Symbol*, Seen> kinds;
        for (const auto& write : model::getModel(l.context().design).proceduralWrites) {
            if (!l.isUser(write.process->location)) continue;
            auto style = styleOf(*write.process);
            if (write.target && !isModuleLevel(*write.target)) continue;
            if (write.called) {
                if (kind_ == BlockingInSeq && style != Style::Sequential) continue;
                if (kind_ == NonblockingInComb && style != Style::Combinational) continue;
                l.reportAt(write.loc, "Unresolved: " + write.reason,
                           {.confidence = Confidence::Unresolved, .hierarchy = write.process->getHierarchicalPath()});
                l.context().degradedReason = write.reason;   // the style of this called write is not decided on this design
                continue;
            }
            if (!write.target) continue;
            l.count();
            auto& seen = kinds[write.target];
            if (!seen.blocking && !seen.nonblocking) seen.loc = write.loc;
            (write.nonBlocking ? seen.nonblocking : seen.blocking) = true;
            if ((kind_ == BlockingInSeq && style == Style::Sequential && !write.nonBlocking) ||
                (kind_ == NonblockingInComb && style == Style::Combinational && write.nonBlocking))
                l.reportAt(write.loc, std::string(write.nonBlocking ? "nonblocking" : "blocking") +
                           " assignment to '" + std::string(write.target->name) + "' in a " +
                           (write.nonBlocking ? "combinational" : "sequential") + " block",
                           {.hierarchy = write.process->getHierarchicalPath(), .signals = {std::string(write.target->name)}});
        }
        if (kind_ == Mixed)
            for (const auto& [sym, seen] : kinds)
                if (seen.blocking && seen.nonblocking)
                    l.reportAt(seen.loc, "'" + std::string(sym->name) + "' is assigned with both blocking and nonblocking assignments",
                               {.hierarchy = sym->getHierarchicalPath(), .signals = {std::string(sym->name)}});
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
    struct V : HardwareVisitor<V, VisitFlags::AllCanonical> {
        Lint& l;
        Kind kind;
        V(Lint& lint, Kind k) : l(lint), kind(k) {}
        // `casex` and `casex ... matches` are both casex statements; the finding is at the keyword (the statement start).
        void casexFinding(const Statement& s, CaseStatementCondition condition) {
            if (kind == CasexStatement && condition == CaseStatementCondition::WildcardXOrZ)
                l.report(s.sourceRange.start(), "casex treats X and Z in the case expression as wildcards",
                         {.nextStep = "Use casez with '?' wildcards in the items, or 'case ... inside'; casex hides X propagation bugs."});
        }
        // A bit that matches anything: casez, only z (`?` is z; an X bit is not a wildcard, that is LNT-040's casez-with-x); casex
        // and `case inside` (wildcard equality, IEEE 1800-2017 11.4.13), x and z.
        static bool wildcardBit(logic_t b, CaseStatementCondition condition) {
            if (condition == CaseStatementCondition::WildcardJustZ) return b.value == logic_t::z.value;
            return condition != CaseStatementCondition::Normal && b.isUnknown();
        }
        // The catch-all shadows later items (WARNING), or only the default (INFO), or nothing at all (INFO).
        void catchAll(slang::SourceLocation at, const std::string& what, bool shadowsItems, bool hasDefault) {
            if (shadowsItems)
                l.report(at, what + " matches every value and shadows later items", {.nextStep = "Use the 'default' item for a catch-all."});
            else
                l.report(at, what + (hasDefault ? " matches every value, so the default item is unreachable" : " matches every value"),
                         {.severity = Severity::Info, .nextStep = "Use the 'default' item for a catch-all."});
        }
        // Whether a pattern matches every value of its type: a wildcard `.*`, a variable `.name`, a structure pattern of such
        // patterns, a tagged pattern of a one-member union, and (under casez or casex) a constant whose bits are all X or Z.
        enum class Match { All, NotAll, Unknown };
        Match matchesAll(const Pattern& p, CaseStatementCondition condition) {
            switch (p.kind) {
                case PatternKind::Wildcard:
                case PatternKind::Variable: return Match::All;
                case PatternKind::Constant: {
                    if (condition == CaseStatementCondition::Normal) return Match::NotAll;
                    auto cv = l.evalConst(p.as<ConstantPattern>().expr);
                    if (cv.bad()) return Match::Unknown;
                    if (!cv.isInteger()) return Match::NotAll;
                    auto& v = cv.integer();
                    for (uint32_t i = 0; i < v.getBitWidth(); i++)
                        if (!wildcardBit(v[i], condition)) return Match::NotAll;
                    return v.getBitWidth() > 0 ? Match::All : Match::NotAll;
                }
                case PatternKind::Structure: {
                    Match r = Match::All;
                    for (auto& f : p.as<StructurePattern>().patterns) {
                        const Match m = matchesAll(*f.pattern, condition);
                        if (m == Match::NotAll) return m;
                        if (m == Match::Unknown) r = m;
                    }
                    return r;
                }
                case PatternKind::Tagged: {
                    auto& t = p.as<TaggedPattern>();
                    const Scope* u = t.member.getParentScope();
                    size_t members = 0;
                    if (u)
                        for (auto& m : u->members())
                            if (m.kind == SymbolKind::Field) members++;
                    if (members != 1) return Match::NotAll;   // it matches one tag of several
                    return t.valuePattern ? matchesAll(*t.valuePattern, condition) : Match::All;
                }
                default: return Match::Unknown;
            }
        }
        void handle(const PatternCaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                casexFinding(c, c.condition);
                if (kind == AllWildcardItem) {
                    bool unknown = false;
                    for (size_t i = 0; i < c.items.size(); i++) {
                        auto& g = c.items[i];
                        if (g.filter) continue;   // a guarded item matches only where its guard holds
                        const Match m = matchesAll(*g.pattern, c.condition);
                        if (m == Match::Unknown) unknown = true;
                        if (m != Match::All) continue;
                        // A constant of wildcard bits is reported where it stands (as for casez); a pattern that is not a
                        // constant matches everything only to shadow what follows it.
                        const bool constant = g.pattern->kind == PatternKind::Constant;
                        if (constant || i + 1 < c.items.size() || c.defaultCase)
                            catchAll(g.pattern->sourceRange.start(), constant ? "case item is all wildcard bits: it" : "pattern", i + 1 < c.items.size(),
                                     c.defaultCase != nullptr);
                    }
                    if (unknown)
                        l.report(c.sourceRange.start(), "whether a pattern of this 'matches' statement matches every value is not determinable",
                                 {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                                  .nextStep = "Check that no earlier pattern shadows a later item."});
                }
            }
            visitDefault(c);
        }
        void handle(const CaseStatement& c) {
            if (l.isUser(c.sourceRange.start())) {
                l.count();
                casexFinding(c, c.condition);
                if (kind == AllWildcardItem && c.condition != CaseStatementCondition::Normal)
                    for (size_t n = 0; n < c.items.size(); n++)
                        for (auto* e : c.items[n].expressions)
                            if (auto cv = l.evalConst(*e); cv.isInteger()) {
                                auto& v = cv.integer();
                                bool allWild = v.getBitWidth() > 0;
                                for (uint32_t i = 0; i < v.getBitWidth() && allWild; i++) allWild = wildcardBit(v[i], c.condition);
                                if (allWild) catchAll(e->sourceRange.start(), "case item is all wildcard bits: it", n + 1 < c.items.size(), c.defaultCase != nullptr);
                            }
            }
            visitDefault(c);
        }
    };
    void check(Lint& l) override {
        V v(l, kind_);
        l.visitHardware(v);
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
