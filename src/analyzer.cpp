#include "sverity/analyzer.h"
#include "sverity/json.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

#include "sverity/waiver.h"
#include "domains/model.h"
#include "domains/relations.h"

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
        for (auto& a : opts.frontend.slangArgs) {
            std::string prefix, path = a;
            if (a.size() > 2 && (a.rfind("-I/", 0) == 0 || a.rfind("-y/", 0) == 0 || a.rfind("-v/", 0) == 0 || a.rfind("-f/", 0) == 0)) {
                prefix = a.substr(0, 2); path = a.substr(2);
            }
            if (!opts.pathRoot.empty() && std::filesystem::path(path).is_absolute()) {
                auto relative = std::filesystem::path(path).lexically_normal().lexically_relative(opts.pathRoot);
                if (!relative.empty()) path = relative.generic_string();
            }
            args.push(prefix + path);
        }
        report.addInput("slang_args", args);
        report.addInput("source_waivers", opts.sourceWaivers);
        report.addInput("source_waiver_scope", "loaded design, include and library buffers only; unloaded library candidates are not read");
        report.addInput("profile", opts.config.profile);
        json::Value goals = json::Value::array();
        for (const auto& goal : opts.config.goals) goals.push(goal);
        report.addInput("goals", goals);
        report.addInput("goal_broadened", opts.config.goalBroadened);
        json::Value dis = json::Value::array();
        for (auto& r : opts.config.disabledRules) dis.push(r);
        report.addInput("disabled_rules", dis);
        json::Value options = json::Value::object();
        auto effective = opts.config.ruleOptions;
        effective.try_emplace("CDC-005.min_stages", "2");
        effective.try_emplace("RDC-006.min_stages", "2");
        effective.try_emplace("LNT-025.priority_chains", "info");
        effective.try_emplace("LNT-036.report_modular", "false");
        effective.try_emplace("LNT-061.allow", "");
        for (auto& [key, value] : effective) options.set(key, value);
        report.addInput("rule_options", options);
        json::Value overrides = json::Value::object();
        for (auto& [key, value] : opts.config.severityOverrides) overrides.set(key, toString(value));
        report.addInput("severity_overrides", overrides);
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
                d.message = "library file '" + std::filesystem::path(f).filename().string() + "' " + why;
                d.nextStep = "Fix the library file; its cells are treated as ordinary modules (no synchroniser is recognised through them).";
                report.add(std::move(d));
                report.markIncomplete("constraints", "library file '" + std::filesystem::path(f).filename().string() + "' " + why);
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
    auto reportPath = [&](const std::string& path) {
        auto absolute = std::filesystem::absolute(path).lexically_normal();
        auto root = opts.pathRoot.empty() ? std::filesystem::current_path() : std::filesystem::absolute(opts.pathRoot);
        return absolute.lexically_relative(root.lexically_normal()).generic_string();
    };
    for (auto& f : opts.sdcFiles) {
        report.addIdentityFile(std::filesystem::absolute(f).string());
        std::string text;
        config.constraintsProvided = true;
        if (!readFile(f, text)) {
            Diagnostic d;
            d.ruleId = "CST-018";
            d.severity = Severity::Error;
            d.identityKey = "unreadable-constraint";
            d.identityArguments = {std::filesystem::absolute(f).string()};
            d.message = "constraint file '" + std::filesystem::path(f).filename().string() + "' could not be read";
            d.nextStep = "Fix the path; no constraints from this file were applied.";
            report.add(std::move(d));
            report.markIncomplete("constraints", "constraint file '" + std::filesystem::path(f).filename().string() + "' unreadable");
            continue;
        }
        config.sdcFiles.push_back(reportPath(f));
        auto parsed = sdc::parse(text, std::filesystem::absolute(f).string());
        for (const auto& sourced : parsed.sourcedFiles) {
            config.sdcFiles.push_back(reportPath(sourced));
            report.addIdentityFile(std::filesystem::absolute(sourced).string());
        }
        config.constraints.merge(std::move(parsed));
    }
    json::Value sdcInputs = json::Value::array();
    for (const auto& file : config.sdcFiles) sdcInputs.push(file);
    report.addInput("sdc_files", sdcInputs);
    if (design && !config.constraints.caseAnalyses.empty()) {
        auto& baseline = model::getModel(*design);
        model::ClockRelations relations(baseline, config);
        const auto& cases = config.constraints.caseAnalyses;
        std::vector<std::set<model::NodeId>> targets(cases.size());
        std::map<model::NodeId, std::set<int>> values;
        design->caseResults.resize(cases.size());
        for (size_t i = 0; i < cases.size(); ++i) {
            for (const auto& ref : cases[i].objects) {
                bool hit = false;
                for (model::NodeId n = 0; n < baseline.nodes.size(); ++n) {
                    if (!relations.nameMatches(ref.kind.empty() ? "get_ports" : ref.kind, ref.pattern,
                                               n, model::stripBit(baseline.nameOf(n)))) continue;
                    hit = true; targets[i].insert(n); values[n].insert(cases[i].value);
                }
                if (!hit) design->caseResults[i].reason = "unknown object '" + ref.pattern + "'; entire declaration is NOT applied";
            }
        }
        // A clock/reset is not an ordinary Boolean data input. Retain its
        // original model until inactivity and asynchronous-event semantics exist.
        std::set<model::NodeId> eventInputs;
        auto ancestors = [&](auto&& self, model::NodeId n, bool clock, std::set<model::NodeId>& seen) -> void {
            if (n == model::kNoNode || !seen.insert(n).second) return;
            eventInputs.insert(n);
            for (auto ei : baseline.in[n]) {
                const auto& edge = baseline.edges[ei];
                if (edge.type == model::EdgeType::Comb && !(clock && edge.op == model::Op::MuxSel))
                    self(self, edge.from, clock, seen);
            }
        };
        for (const auto& flop : baseline.flops) {
            std::set<model::NodeId> seen;
            ancestors(ancestors, flop.clockSignal, true, seen);
            for (const auto& reset : flop.resets) {
                seen.clear(); ancestors(ancestors, reset.signal, false, seen);
            }
        }
        auto protectEvents = [&](const std::vector<model::NodeId>& nodes) {
            for (auto n : nodes) { std::set<model::NodeId> seen; ancestors(ancestors, n, false, seen); }
        };
        for (const auto& ram : baseline.rams) { protectEvents(ram.writeClk); protectEvents(ram.readClk); }
        for (const auto& gate : baseline.clockGates) { protectEvents(gate.clk); protectEvents(gate.enable); }
        for (const auto& instance : baseline.libInstances)
            for (const auto& port : instance.ports)
                if (port.role == model::PortRole::Clock || port.role == model::PortRole::Reset) protectEvents(port.nodes);
        for (size_t i = 0; i < cases.size(); ++i) {
            auto& result = design->caseResults[i];
            for (auto n : targets[i]) {
                if (values[n].size() > 1) { result.reason = "conflicting values on '" + baseline.nameOf(n) + "'; apply nothing"; break; }
                else if (baseline.symbolWidth(n) != 1) result.reason = "multi-bit objects are not supported; no scalar-to-vector interpretation is assumed";
                else if (!baseline.in[n].empty() || baseline.constDrivers.count(n) || baseline.selfComb.count(n) ||
                         baseline.blackBoxDriven[n] || baseline.unresolvedDriven[n]) result.reason = "object is driven inside the design; no driver is overwritten";
                else if (!baseline.topInput[n]) result.reason = "internal targets are not forced: driver replacement and instance-local constant propagation are not established";
                else if (eventInputs.count(n) || !relations.clockNameOfNode(n).empty()) result.reason = "clock/reset activity semantics are not established; constant event sources remain unapplied (not reclassified as active clocks)";
            }
            if (result.reason.empty() && !targets[i].empty()) {
                result.applied = true;
                for (auto n : targets[i]) {
                    design->caseConstants[baseline.nodes[n].sym] = cases[i].value;
                    result.names.push_back(baseline.nameOf(n));
                }
            }
        }
        if (!design->caseConstants.empty()) {
            // Propagate only single unconditional scalar continuous drivers.
            // Shared elaboration symbols across instances cannot use a global value.
            std::map<const slang::ast::ValueSymbol*, int> counts;
            std::map<model::NodeId, int> known;
            for (model::NodeId n = 0; n < baseline.nodes.size(); ++n) {
                ++counts[baseline.nodes[n].sym];
                auto it = design->caseConstants.find(baseline.nodes[n].sym);
                if (it != design->caseConstants.end()) known[n] = it->second;
            }
            auto evaluate = [&](auto&& self, const model::BoolFactPtr& fact) -> int {
                if (!fact || fact->opaque) return -1;
                using K = model::BoolFact::Kind;
                if (fact->kind == K::Constant) return fact->value;
                if (fact->kind == K::Node) { auto it = known.find(fact->node); return it == known.end() ? -1 : it->second; }
                if (fact->kind == K::Not && fact->args.size() == 1) {
                    int v = self(self, fact->args[0]); return v < 0 ? -1 : !v;
                }
                if (fact->kind == K::And || fact->kind == K::Or) {
                    bool unknown = false;
                    for (const auto& arg : fact->args) {
                        int v = self(self, arg);
                        if (v < 0) unknown = true;
                        else if (v == (fact->kind == K::Or)) return v;
                    }
                    return unknown ? -1 : fact->kind == K::And;
                }
                return -1;
            };
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& [n, drivers] : baseline.booleanDrivers) {
                    if (known.count(n) || drivers.size() != 1 || !drivers[0].unconditional ||
                        baseline.symbolWidth(n) != 1 || counts[baseline.nodes[n].sym] != 1 ||
                        baseline.flopOfQ.count(n) || baseline.selfComb.count(n) || baseline.blackBoxDriven[n] ||
                        baseline.unresolvedDriven[n] || baseline.uncertainCallDrivers.count(n)) continue;
                    bool continuous = !baseline.in[n].empty() || baseline.constDrivers.count(n);
                    auto validProc = [&](uint32_t proc) { auto k = baseline.procs[proc].kind;
                        return k == model::ProcKind::Continuous || k == model::ProcKind::PortConnection; };
                    for (auto ei : baseline.in[n]) continuous &= validProc(baseline.edges[ei].proc);
                    if (auto it = baseline.constDrivers.find(n); it != baseline.constDrivers.end())
                        for (const auto& driver : it->second) continuous &= validProc(driver.proc);
                    if (!continuous) continue;
                    int v = evaluate(evaluate, drivers[0].value);
                    if (v >= 0) { known[n] = v; design->caseConstants[baseline.nodes[n].sym] = v; changed = true; }
                }
            }
            design->model.reset();
        }
    }
    runRules(design.get(), report, config);
    if (design && !design->caseConstants.empty()) {
        json::Value modes = json::Value::array();
        for (size_t i = 0; i < design->caseResults.size(); ++i)
            for (const auto& name : design->caseResults[i].names) {
                json::Value mode = json::Value::object();
                mode.set("object", name).set("value", config.constraints.caseAnalyses[i].value);
                modes.push(mode);
            }
        report.addInput("case_analysis", modes);
        auto& selected = model::getModel(*design);
        report.addInput("case_pruned_branches", static_cast<int>(selected.casePrunedBranches));
        report.addInput("case_pruned_expressions", static_cast<int>(selected.casePrunedExpressions));
    }
    report.finalize();

    WaiverSet waivers;
    if (design && opts.sourceWaivers) waivers = sourceWaivers(*design, opts.pathRoot);
    bool haveWaivers = false;
    for (auto& wf : opts.waiverFiles) {
        std::string text, err;
        WaiverSet one;
        if (!readFile(wf, text) || !WaiverSet::parse(text, one, err, std::filesystem::path(wf).filename().string())) {
            Diagnostic d;
            d.ruleId = "CST-006";
            d.severity = Severity::Error;
            d.identityKey = "unreadable-waiver";
            d.identityArguments = {std::filesystem::absolute(wf).string()};
            d.message = "waiver file '" + std::filesystem::path(wf).filename().string() + "' could not be loaded" + (err.empty() ? "" : ": " + err);
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
    // Fingerprints and waivers use the original diagnostic identity. Overrides
    // are applied only after waiver matching and never alter coverage.
    for (auto& d : report.mutableDiagnostics()) {
        auto it = config.severityOverrides.find(d.ruleId);
        if (it == config.severityOverrides.end()) continue;
        d.originalSeverity = d.severity;
        d.requestedSeverity = it->second;
        Severity effective = it->second;
        if (effective < d.severity && (d.severity == Severity::Fatal ||
            (d.origin == "slang" && d.severity >= Severity::Error) ||
            (d.severity >= Severity::Error && (d.ruleId == "LNT-001" || d.ruleId == "CST-006" || d.ruleId == "CST-018")))) {
            effective = d.severity;
            d.severityOverrideReason = "fatal, frontend or invalid input errors cannot be downgraded";
        } else if (effective < d.severity && effective < Severity::Warning &&
                   (d.confidence == Confidence::Unresolved || d.proofState == ProofState::Unknown || d.proofState == ProofState::NotAttempted)) {
            effective = Severity::Warning;
            d.severityOverrideReason = "unresolved findings, unknown proofs and obligations have a WARNING floor";
        }
        d.severity = effective;
        if (d.disposition != Disposition::Waived && d.disposition != Disposition::Resolved) {
            if (effective == Severity::Warning || (d.originalSeverity >= Severity::Error && effective < Severity::Error))
                d.disposition = Disposition::ReviewRequired;
            else if (effective >= Severity::Error) d.disposition = Disposition::Open;
        }
    }
    return report;
}

} // namespace sverity
