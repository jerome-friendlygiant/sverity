#include "sverity/analyzer.h"
#include "sverity/json.h"

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
    if (design) {
        for (auto& f : opts.libraryFiles) {
            std::string text;
            auto fail = [&](const std::string& why) {
                Diagnostic d;
                d.ruleId = "CST-018";
                d.severity = Severity::Error;
                d.message = "library file '" + f + "' " + why;
                d.nextStep = "Fix the library file; its cells are treated as ordinary modules (no synchroniser is recognised through them).";
                report.add(std::move(d));
                report.markIncomplete("constraints", "library file '" + f + "' " + why);
            };
            if (!readFile(f, text)) { fail("could not be read"); continue; }
            auto pr = json::parse(text);
            if (!pr.ok) { fail("is not valid JSON (" + pr.error + ")"); continue; }
            for (auto& c : pr.value["cells"].asArray()) {
                LibCell lc;
                lc.name = c["name"].asString();
                if (c.find("kind")) lc.kind = c["kind"].asString();
                lc.clock = c["clock"].asString();
                lc.input = c["input"].asString();
                lc.output = c["output"].asString();
                lc.enable = c["enable"].asString();
                lc.reset = c["reset"].asString();
                lc.writeClock = c["write_clock"].asString();
                lc.readClock = c["read_clock"].asString();
                if (c.find("stages")) lc.stages = static_cast<int>(c["stages"].asInt(2));
                if (lc.kind == "flop") lc.stages = 1;
                if (c.find("edge")) lc.edge = c["edge"].asString() == "negedge" ? 2 : 1;
                if (c.find("reset_active")) lc.resetActiveLow = c["reset_active"].asString() != "high";
                if (c.find("reset_value")) lc.resetValue = c["reset_value"].asInt(0) ? '1' : '0';
                if (c.find("enable_latched")) lc.enableLatched = c["enable_latched"].asBool(false);
                if (c.find("sync_read")) lc.syncRead = c["sync_read"].asBool(true);
                if (c.find("ports"))
                    for (auto& p : c["ports"].asArray()) lc.ports.push_back({p["name"].asString(), p["dir"].asString(), p["role"].asString(), p["clock"].asString()});
                bool ok = !lc.name.empty() && lc.stages >= 1;
                if (lc.kind == "synchronizer" || lc.kind == "flop") ok = ok && !lc.clock.empty() && !lc.input.empty() && !lc.output.empty();
                else if (lc.kind == "clock_gate") ok = ok && !lc.clock.empty() && !lc.enable.empty() && !lc.output.empty();
                else if (lc.kind == "ram") ok = ok && !lc.writeClock.empty() && !lc.readClock.empty() && !lc.ports.empty();
                else if (lc.kind == "blackbox") ok = ok && !lc.ports.empty();
                else ok = false;
                for (auto& p : lc.ports)
                    ok = ok && !p.name.empty() && (p.dir == "in" || p.dir == "out" || p.dir == "inout");
                if (!ok) {
                    fail("has a cell entry that is incomplete or of an unsupported kind (kinds: synchronizer, flop, clock_gate, ram, blackbox; see LibCell in sverity/frontend.h)");
                    continue;
                }
                design->libCells[lc.name] = lc;
            }
        }
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
