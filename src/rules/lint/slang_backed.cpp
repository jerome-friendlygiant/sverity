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
    full("LNT-001");
    full("LNT-006");
    full("LNT-007");
    full("LNT-037");
    full("LNT-002");   // slang errors: unknown module/package/symbol, redefinition
    full("LNT-003");   // slang errors: illegal port connections (unconnected ports are LNT-010)
    full("LNT-014");   // slang errors: always_ff/always_comb misuse
    full("LNT-017");   // slang case-enum / case-enum-explicit
    full("LNT-031");   // slang errors: illegal assignment targets
    full("LNT-050");   // slang errors: direction / modport misuse
    full("LNT-004");   // slang width-trunc/-expand and port-width-* diagnostics; parameter-dependent truncation is also LNT-057
    full("LNT-005");   // slang sign-conversion/sign-compare/implicit-conv
    full("LNT-065");   // slang case-default/-incomplete/-none/-not-wildcard/-wildcard-2state/-too-complex
    full("LNT-059");   // slang unused-import/-package-*, shadowing, duplicate-import
    return v;
}

} // namespace sverity
