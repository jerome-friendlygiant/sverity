#include <map>

#include "sverity/rule.h"

namespace sverity {

void runRules(Design* design, Report& report, const Config& config) {
    const auto& catalog = Catalog::embedded();
    auto rules = makeAllRules();
    std::map<std::string, Rule*, std::less<>> byId;
    for (auto& r : rules) byId[std::string(r->id())] = r.get();
    report.declareScope("rule-execution");

    uint32_t counts[6] = {0, 0, 0, 0, 0, 0};
    std::map<std::string, std::map<RuleState, uint32_t>> perCategory;

    for (const auto& ri : catalog.rules()) {
        RuleCoverage rc;
        rc.ruleId = ri.id;
        rc.catalogStatus = ri.status;
        auto it = byId.find(ri.id);
        if (config.disabledRules.count(ri.id)) {
            rc.state = RuleState::Disabled;
            rc.reason = "disabled by configuration";
            if (it != byId.end() && it->second->upstreamBacked())
                rc.reason += "; NOTE: slang still produces this rule's findings and they remain in the report "
                             "(disabling never hides a finding)";
        } else if (it == byId.end()) {
            rc.state = RuleState::Unimplemented;
            rc.reason = "no native implementation in this build";
        } else if (!design) {
            rc.state = RuleState::NotRun;
            rc.reason = "the design could not be loaded";
        } else {
            RuleContext ctx{*design, report, config, 0, {}};
            try {
                it->second->run(ctx);
                rc.objectsEvaluated = ctx.objectsEvaluated;
                if (!ctx.degradedReason.empty()) {
                    rc.state = RuleState::Partial;
                    rc.reason = ctx.degradedReason;
                } else {
                    rc.state = it->second->completeness();
                    if (rc.state == RuleState::Partial) rc.reason = it->second->partialReason();
                }
            } catch (const std::exception& e) {
                rc.state = RuleState::Failed;
                rc.reason = std::string("internal failure: ") + e.what();
                Diagnostic d;
                d.ruleId = ri.id;
                d.severity = Severity::Fatal;
                d.message = "rule implementation failed: " + std::string(e.what());
                d.nextStep = "Report this as a SVerity bug; results for this rule are not available.";
                report.add(std::move(d));
            }
        }
        if (rc.state == RuleState::Partial) report.markIncomplete("rule-execution", ri.id + ": " + rc.reason);
        counts[static_cast<int>(rc.state)]++;
        perCategory[ri.category][rc.state]++;
        report.addRuleCoverage(std::move(rc));
    }

    for (auto& id : config.disabledRules) {
        if (catalog.find(id)) continue;
        Diagnostic d;
        d.ruleId = "CST-019";
        d.severity = Severity::Warning;
        d.message = "disabled-rule list names unknown rule '" + id + "'";
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Fix the rule ID; a misspelt ID leaves the intended rule enabled or the policy unclear.";
        report.add(std::move(d));
    }

    // CST-019: an advertised-but-not-evaluated rule is a coverage gap, never silence.
    uint32_t gaps = counts[static_cast<int>(RuleState::Partial)] + counts[static_cast<int>(RuleState::Disabled)] +
                    counts[static_cast<int>(RuleState::Unimplemented)] + counts[static_cast<int>(RuleState::NotRun)] +
                    counts[static_cast<int>(RuleState::Failed)];
    if (gaps) {
        Diagnostic d;
        d.ruleId = "CST-019";
        d.severity = Severity::Warning;
        d.message = std::to_string(gaps) + " of " + std::to_string(catalog.rules().size()) +
                    " catalogued rules were not fully evaluated in this run (" +
                    std::to_string(counts[static_cast<int>(RuleState::Unimplemented)]) + " unimplemented, " +
                    std::to_string(counts[static_cast<int>(RuleState::Partial)]) + " partial, " +
                    std::to_string(counts[static_cast<int>(RuleState::Disabled)]) + " disabled, " +
                    std::to_string(counts[static_cast<int>(RuleState::NotRun)]) + " not run, " +
                    std::to_string(counts[static_cast<int>(RuleState::Failed)]) +
                    " failed); the absence of findings for them is NOT evidence of safety";
        for (auto& [cat, m] : perCategory) {
            std::string t;
            for (auto& [st, n] : m)
                if (st != RuleState::Executed) t += std::string(toString(st)) + "=" + std::to_string(n) + " ";
            if (!t.empty()) d.evidence.push_back({"coverage_gap_" + cat, t, {}});
        }
        d.confidence = Confidence::Unresolved;
        d.nextStep = "See coverage.rules in the JSON report for the per-rule state; analysis_coverage is INCOMPLETE.";
        report.add(std::move(d));
    }
}

} // namespace sverity
