// Rules whose analysis is performed by slang's own compilation/analysis passes (the sole mandatory
// HDL frontend). The frontend converts those diagnostics into SVerity findings (see
// frontend.cpp / ruleForSlangOption). These rule objects only account for coverage honestly:
//  * Executed  - slang's check covers the whole catalog title.
//  * Partial   - slang covers a subset only; the gap is spelled out and keeps coverage INCOMPLETE.
// They are slang-backed, not a delegation to a second tool: slang is linked into SVerity.
#include "slang/analysis/AnalysisManager.h"
#include "rules/rules.h"

namespace sverity {
namespace {

class SlangBackedRule final : public Rule {
public:
    SlangBackedRule(std::string id, bool full, std::string gap)
        : id_(std::move(id)), full_(full), gap_(std::move(gap)) {}
    std::string_view id() const override { return id_; }
    RuleState completeness() const override { return full_ ? RuleState::Executed : RuleState::Partial; }
    std::string partialReason() const override { return gap_; }
    bool upstreamBacked() const override { return true; }
    void run(RuleContext& ctx) override {
        ctx.objectsEvaluated = ctx.design.analysis ? ctx.design.analysis->getStats().numScopes : 0;
        for (auto& s : ctx.report.scopes())
            if (s.scope == "upstream-diagnostics" && s.coverage == AnalysisCoverage::Incomplete)
                ctx.degradedReason = "slang diagnostics were disabled or limited by command-line options";
        if (ctx.design.numUpstreamErrors > 0 && ctx.degradedReason.empty())
            ctx.degradedReason = "design has elaboration errors; slang checks may be skipped on erroneous code";
    }

private:
    std::string id_;
    bool full_;
    std::string gap_;
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeSlangBackedRules() {
    std::vector<std::unique_ptr<Rule>> v;
    auto full = [&](const char* id) { v.push_back(std::make_unique<SlangBackedRule>(id, true, "")); };
    auto part = [&](const char* id, const char* gap) {
        v.push_back(std::make_unique<SlangBackedRule>(id, false, gap));
    };
    full("LNT-001");
    full("LNT-006");
    full("LNT-007");
    full("LNT-010");
    full("LNT-037");
    part("LNT-004", "slang width diagnostics only; no cross-hierarchy parameterised-interface analysis (see LNT-023/057)");
    part("LNT-005", "slang signedness diagnostics only; implicit conversion hazards beyond slang's checks not covered");
    part("LNT-008", "slang undriven-net/variable/port diagnostics only; bit-range precision of slang not independently validated");
    part("LNT-009", "slang multiple-driver diagnostics only; no cross-module/hierarchy driver conflict analysis");
    part("LNT-011", "slang unused-* diagnostics only; no deliberate-unused suppression policy (no silent suppression by design)");
    part("LNT-013", "slang inferred-latch diagnostic only; no incomplete-assignment dataflow beyond slang");
    part("LNT-016", "slang case diagnostics only; no casex/broad-wildcard policy configuration");
    part("LNT-026", "slang shift-count diagnostics only; constant sizing/unsized-literal hazards not covered");
    part("LNT-032", "slang case-dup/overlap diagnostics only");
    part("LNT-035", "slang shift-signedness diagnostics only; unsized literal part not covered");
    part("LNT-056", "slang unsized-in-concat only; constant width mismatch in replication not covered");
    part("LNT-059", "slang unused-import and shadowing diagnostics only");
    part("STY-010", "slang unnamed-generate only; instance/generate naming policy not covered");
    return v;
}

} // namespace sverity
