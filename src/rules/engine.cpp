#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <vector>

#include "domains/model.h"
#include "rules/rules.h"
#include "sverity/rule.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"

namespace sverity {

namespace {
/// Resident set now (not the peak), for the model's share of the process.
uint64_t currentRssKb() {
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0, resident = 0;
    statm >> size >> resident;
    return resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) / 1024;
}
} // namespace

void runRules(Design* design, Report& report, const Config& config) {
    const auto& catalog = Catalog::embedded();
    auto rules = makeAllRules();
    std::map<std::string, Rule*, std::less<>> byId;
    for (auto& r : rules) byId[std::string(r->id())] = r.get();
    report.declareScope("rule-execution");
    // Build the model up front when measuring, so its cost is not charged to whichever rule happens to run first.
    if (config.timings && design) {
        const auto start = Timings::Clock::now();
        config.timings->count("rss_kb_before_model", currentRssKb());   // the load and the slang AST
        try {
            if (!design->model) design->model = std::shared_ptr<model::Model>(model::buildModel(*design).release());
            const auto& model = *design->model;
            config.timings->record("model", start);
            config.timings->count("graph_nodes", model.nodes.size());
            config.timings->count("graph_edges", model.edges.size());
            config.timings->count("register_bits", model.flops.size());
            config.timings->count("rss_kb_after_model", currentRssKb());
            for (const auto& [name, bytes] : model::modelMemory(model)) config.timings->count(name, bytes);
        } catch (const std::exception&) {
            config.timings->record("model_failed", start);   // the rules that need it report the failure
        }
    }

    // Definitions no instance of the design uses are not hardware of this run: say which, so absence of findings there is not read as
    // safety.
    if (design && design->compilation) {
        std::vector<std::string> unused;
        for (const auto& member : design->compilation->getRoot().members())
            if (const auto* inst = member.as_if<slang::ast::InstanceSymbol>(); inst && inst->body.flags.has(slang::ast::InstanceFlags::Uninstantiated))
                unused.emplace_back(inst->getDefinition().name);
        std::sort(unused.begin(), unused.end());
        unused.erase(std::unique(unused.begin(), unused.end()), unused.end());
        report.setObjectCount("uninstantiated_definitions", unused.size());
        report.setUninstantiatedDefinitions(std::move(unused));
    }
    const int requestsBefore = design ? design->modelRequests : 0;
    uint32_t counts[6] = {0, 0, 0, 0, 0, 0};
    std::map<std::string, std::map<RuleState, uint32_t>> perCategory;

    // Runs one rule (or records why it did not run) and returns its coverage record.
    auto evaluate = [&](const std::string& id, const std::string& status, Rule* rule) {
        RuleCoverage rc;
        rc.ruleId = id;
        rc.catalogStatus = status;
        if (config.disabledRules.count(id)) {
            rc.state = RuleState::Disabled;
            rc.reason = "disabled by configuration";
            if (rule && rule->upstreamBacked())
                rc.reason += "; NOTE: slang still produces this rule's findings and they remain in the report "
                             "(disabling never hides a finding)";
        } else if (!rule) {
            rc.state = RuleState::Unimplemented;
            rc.reason = "no native implementation in this build";
        } else if (!design) {
            rc.state = RuleState::NotRun;
            rc.reason = "the design could not be loaded";
        } else {
            RuleContext ctx{*design, report, config, 0, {}};
            const auto start = Timings::Clock::now();
            struct RecordRule {
                const Config& config; const std::string& id; Timings::Clock::time_point start;
                ~RecordRule() { if (config.timings) config.timings->record("rule:" + id, start); }
            } recordRule{config, id, start};
            try {
                rule->run(ctx);
                rc.objectsEvaluated = ctx.objectsEvaluated;
                if (!ctx.degradedReason.empty()) {
                    rc.state = RuleState::Partial;
                    rc.reason = ctx.degradedReason;
                    rc.degraded = true;   // lost on THIS design: unlike a rule that is partial in the tool version
                } else {
                    rc.state = rule->completeness();
                    if (rc.state == RuleState::Partial) rc.reason = rule->partialReason();
                }
            } catch (const std::exception& e) {
                rc.state = RuleState::Failed;
                rc.reason = std::string("internal failure: ") + e.what();
                Diagnostic d;
                d.ruleId = id;
                d.severity = Severity::Fatal;
                d.message = "rule implementation failed: " + std::string(e.what());
                d.nextStep = "Report this as a SVerity bug; results for this rule are not available.";
                report.add(std::move(d));
            }
        }
        if (const auto* info = catalog.find(id); !config.deck.empty() && info && !inDeck(*info, config.deck)) rc.preview = true;
        // Only a loss on this design marks the run incomplete. A rule that is partial or unimplemented in the tool version is
        // a statement about the tool (Report::toolCoverage), the same on every run.
        if (rc.degraded && !rc.preview) report.markIncomplete("rule-execution", id + ": " + rc.reason);
        if (!rc.preview) counts[static_cast<int>(rc.state)]++;   // a preview rule is not part of what this run claims
        return rc;
    };

    for (const auto& ri : catalog.rules()) {
        auto it = byId.find(ri.id);
        RuleCoverage rc = evaluate(ri.id, ri.status, it == byId.end() ? nullptr : it->second);
        if (!rc.preview) perCategory[ri.category][rc.state]++;
        report.addRuleCoverage(std::move(rc));
    }
    // Custom rules (registerRule) whose id is not in the catalog are run and listed too.
    for (auto& [id, rule] : byId) {
        if (catalog.find(id)) continue;
        RuleCoverage rc = evaluate(id, "custom", rule);
        perCategory["custom"][rc.state]++;
        report.addRuleCoverage(std::move(rc));
    }

    if (design) demoteSimulationRegionFindings(*design, report);

    // Any enabled rule that read the shared connectivity model (the lint rules among them, not only the CDC/CST passes) relies on
    // it: a black box or a construct it falls back on is a gap of this run whichever rule asked.
    if (design && design->model && design->modelRequests > requestsBefore) {
        const auto& m = *design->model;
        report.declareScope("blackboxes");
        report.declareScope("graph-precision");
        std::set<std::string> boxes;
        for (const auto& b : m.blackBoxes) boxes.insert(b.module);
        for (const auto& mod : boxes) report.markIncomplete("blackboxes", "unmodeled black box '" + mod + "'");
        std::map<std::string, size_t> fallbacks;
        for (const auto& f : m.fallbacks) ++fallbacks[f.category];
        for (const auto& [cat, n] : fallbacks) report.markIncomplete("graph-precision", cat + ": " + std::to_string(n) + " occurrence(s)");
    }

    for (auto& id : config.disabledRules) {
        if (catalog.find(id)) continue;
        Diagnostic d;
        d.ruleId = "CST-019";
        d.severity = Severity::Warning;
        d.identityKey = id;
        d.message = "disabled-rule list names unknown rule '" + id + "'";
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Fix the rule ID; a misspelt ID leaves the intended rule enabled or the policy unclear.";
        report.add(std::move(d));
    }

    // CST-019: a rule that could not give its answer on THIS design (it failed, did not run, or ran degraded) is a coverage gap,
    // never silence. A rule that is partial or unimplemented in the tool version, or that the user disabled, is not a loss of
    // this run: Report::toolCoverage and coverage.rules state it, and --exit-policy strict still blocks on it.
    uint32_t degraded = 0, lost = counts[static_cast<int>(RuleState::NotRun)] + counts[static_cast<int>(RuleState::Failed)];
    for (const auto& rc : report.ruleCoverage()) degraded += rc.degraded && !rc.preview;
    lost += degraded;
    if (lost) {
        Diagnostic d;
        d.ruleId = "CST-019";
        d.severity = Severity::Warning;
        d.message = std::to_string(lost) + " of " + std::to_string(counts[0] + counts[1] + counts[2] + counts[3] + counts[4] + counts[5]) +
                    " catalogued rules did not give their answer on this design (" +
                    std::to_string(degraded) + " degraded, " +
                    std::to_string(counts[static_cast<int>(RuleState::NotRun)]) + " not run, " +
                    std::to_string(counts[static_cast<int>(RuleState::Failed)]) +
                    " failed)" + (config.deck.empty() ? "" : " in deck " + config.deck) + "; the absence of findings for them is NOT evidence of safety";
        for (auto& [cat, m] : perCategory) {
            std::string t;
            for (auto& [st, n] : m)
                if (st == RuleState::NotRun || st == RuleState::Failed) t += std::string(toString(st)) + "=" + std::to_string(n) + " ";
            if (!t.empty()) d.evidence.push_back({"coverage_gap_" + cat, t, {}});
        }
        d.confidence = Confidence::Unresolved;
        d.nextStep = "See coverage.rules in the JSON report for the per-rule state; analysis_coverage is INCOMPLETE.";
        report.add(std::move(d));
    }
}

} // namespace sverity
