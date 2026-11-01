#include "sverity/analyzer.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "sverity/waiver.h"

namespace sverity {

std::string currentDateIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

Report analyze(const AnalyzerOptions& opts) {
    Report report;
    report.setToolInfo(SVERITY_VERSION, slangVersionString(), Catalog::embedded().version());
    report.setPathRoot(opts.pathRoot);
    {
        json::Value args = json::Value::array();
        for (auto& a : opts.frontend.slangArgs) args.push(a);
        report.addInput("slang_args", args);
        report.addInput("profile", opts.config.profile);
        json::Value dis = json::Value::array();
        for (auto& r : opts.config.disabledRules) dis.push(r);
        report.addInput("disabled_rules", dis);
        report.addInput("upstream_warnings", opts.frontend.enableAllUpstreamWarnings ? "everything" : "slang-default");
    }
    // The elaborated design lives as long as the rules need it.
    std::unique_ptr<Design> design;
    try {
        design = loadDesign(opts.frontend, report);
    } catch (const std::exception& e) {
        Diagnostic d;
        d.ruleId = "LNT-001";
        d.severity = Severity::Fatal;
        d.message = std::string("internal failure while loading the design: ") + e.what();
        report.setExecutionError(d.message);
        report.add(std::move(d));
        report.markIncomplete("elaboration", "internal failure while loading the design");
    }
    Config config = opts.config;
    for (auto& f : opts.sdcFiles) {
        std::string text;
        config.constraintsProvided = true;
        if (!readFile(f, text)) {
            Diagnostic d;
            d.ruleId = "CST-018";
            d.severity = Severity::Error;
            d.message = "constraint file '" + f + "' could not be read";
            d.nextStep = "Fix the path; no constraints from this file were applied.";
            report.add(std::move(d));
            report.markIncomplete("constraints", "constraint file '" + f + "' unreadable");
            continue;
        }
        config.sdcFiles.push_back(f);
        config.constraints.merge(sdc::parse(text, std::filesystem::path(f).filename().string()));
    }
    runRules(design.get(), report, config);
    report.finalize();

    WaiverSet waivers;
    bool haveWaivers = false;
    for (auto& wf : opts.waiverFiles) {
        std::string text, err;
        WaiverSet one;
        if (!readFile(wf, text) || !WaiverSet::parse(text, one, err, std::filesystem::path(wf).filename().string())) {
            Diagnostic d;
            d.ruleId = "CST-006";
            d.severity = Severity::Error;
            d.message = "waiver file '" + wf + "' could not be loaded" + (err.empty() ? "" : ": " + err);
            d.nextStep = "Fix the waiver file; no waiver from it was applied.";
            report.add(std::move(d));
            report.setExecutionError("waiver file could not be loaded");
            continue;
        }
        for (auto& w : one.waivers()) waivers.mutableWaivers().push_back(w);
        haveWaivers = true;
    }
    (void)haveWaivers;
    applyWaivers(report, waivers, opts.today.empty() ? currentDateIso() : opts.today);
    for (auto& rc : report.mutableRuleCoverage())
        if (rc.ruleId == "CST-006" || rc.ruleId == "CST-022") rc.objectsEvaluated = waivers.waivers().size();
    report.finalize();
    return report;
}

} // namespace sverity
