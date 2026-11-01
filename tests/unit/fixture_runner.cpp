// Fixture-driven conformance runner. Each fixture is a SystemVerilog file annotated with comments:
//   // sverity-args: <extra slang args, e.g. --top top>
//   // expect: RULE-ID SEVERITY [line]        (a finding must exist)
//   // expect-none: RULE-ID [SEVERITY]        (no finding of that rule (and severity) may exist: "good" case)
//   // expect-coverage: <scope> INCOMPLETE    (scope must have exactly that coverage value)
//   // expect-count: RULE-ID SEVERITY N       (total occurrence count of that rule+severity must equal N)
// In sverity-args, @DIR@ expands to the fixture's directory and @FIXTURES@ to the fixtures root (shared files live in
// fixtures/_common). Usage: fixture_runner <fixtures dir> [path substring filter, e.g. CDC-003 or mutations].
// Fixture directories are named after the rule they exercise: fixtures/<RULE-ID>/{good,bad,...}_*.sv
// Report-integrity invariants are checked for every fixture.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "sverity/analyzer.h"

namespace fs = std::filesystem;
using namespace sverity;

static std::vector<std::string> split(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> v;
    for (std::string w; is >> w;) v.push_back(w);
    return v;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: fixture_runner <fixture dir>\n"; return 2; }
    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(argv[1]))
        if (e.is_regular_file() && (e.path().extension() == ".sv" || e.path().extension() == ".v") &&
            (argc < 3 || e.path().string().find(std::string("/") + argv[2] + "/") != std::string::npos))
            files.push_back(e.path());
    if (files.empty()) { std::cerr << "no fixtures matched\n"; return 2; }
    std::sort(files.begin(), files.end());
    int failed = 0, total = 0;
    for (auto& f : files) {
        total++;
        std::ifstream in(f);
        std::vector<std::string> expects, nones, covs, counts, args;
        for (std::string line; std::getline(in, line);) {
            auto grab = [&](const char* key, std::vector<std::string>* out) {
                auto p = line.find(key);
                if (p != std::string::npos) out->push_back(line.substr(p + std::string(key).size()));
            };
            grab("// expect: ", &expects); grab("// expect-none: ", &nones); grab("// expect-coverage: ", &covs); grab("// expect-count: ", &counts);
            std::vector<std::string> a; grab("// sverity-args: ", &a);
            for (auto& x : a)
                for (auto w : split(x)) {
                    for (size_t p; (p = w.find("@DIR@")) != std::string::npos;) w.replace(p, 5, f.parent_path().string());
                    for (size_t p; (p = w.find("@FIXTURES@")) != std::string::npos;) w.replace(p, 10, std::string(argv[1]));
                    args.push_back(w);
                }
        }
        AnalyzerOptions o;
        for (size_t i = 0; i < args.size(); i++) {
            const std::string& a = args[i];
            if (a == "--slang-default-warnings") o.frontend.enableAllUpstreamWarnings = false;   // SVerity options
            else if (a == "--sdc" && i + 1 < args.size()) o.sdcFiles.push_back(args[++i]);
            else if (a == "--option" && i + 1 < args.size()) {                                    // --option KEY=VALUE
                auto kv = args[++i];
                auto eq = kv.find('=');
                if (eq != std::string::npos) o.config.ruleOptions[kv.substr(0, eq)] = kv.substr(eq + 1);
            }
            else o.frontend.slangArgs.push_back(a);
        }
        o.frontend.slangArgs.push_back(f.string());
        o.pathRoot = fs::path(argv[1]).parent_path().string();
        o.today = "2026-11-01";
        Report r = analyze(o);
        std::vector<std::string> errs;
        auto has = [&](const std::string& rule, const std::string& sev, uint32_t line) {
            for (auto& d : r.diagnostics())
                if (d.ruleId == rule && (sev.empty() || sev == toString(d.severity)) && (!line || d.location.line == line))
                    return true;
            return false;
        };
        for (auto& e : expects) {
            auto w = split(e);
            if (w.size() < 2) { errs.push_back("bad expect line: " + e); continue; }
            uint32_t line = w.size() > 2 ? static_cast<uint32_t>(std::stoul(w[2])) : 0;
            if (!has(w[0], w[1], line)) errs.push_back("missing expected finding " + e);
        }
        for (auto& n : nones) {
            auto w = split(n);
            if (!w.empty() && has(w[0], w.size() > 1 ? w[1] : "", 0)) errs.push_back("unexpected finding for " + n);
        }
        for (auto& c : covs) {
            auto w = split(c);
            bool ok = false;
            for (auto& s : r.scopes()) if (w.size() >= 2 && s.scope == w[0] && std::string(toString(s.coverage)) == w[1]) ok = true;
            if (!ok) errs.push_back("scope coverage expectation not met: " + c);
        }
        for (auto& c : counts) {
            auto w = split(c);
            if (w.size() != 3) { errs.push_back("bad expect-count line: " + c); continue; }
            uint32_t n = 0;
            for (auto& d : r.diagnostics()) if (d.ruleId == w[0] && w[1] == toString(d.severity)) n += d.count;
            if (n != std::stoul(w[2])) errs.push_back("count of " + w[0] + " " + w[1] + " is " + std::to_string(n) + ", expected " + w[2]);
        }
        // ---- report integrity invariants (every fixture)
        auto js = r.toJson();
        auto reparsed = json::parse(js.dump());
        if (!reparsed.ok) errs.push_back("JSON report does not round-trip");
        if (r.diagnostics().empty() && js["diagnostics"].asArray().size() != 0) errs.push_back("diagnostic count mismatch");
        if (js["summary"]["analysis_coverage"].asString().empty() || js["summary"]["review_status"].asString().empty() ||
            js["summary"]["execution_status"].asString().empty())
            errs.push_back("summary dimension missing");
        if (js["coverage"]["rules"].asArray().size() != Catalog::embedded().rules().size())
            errs.push_back("coverage does not list every catalog rule");
        if (r.analysisCoverage() == AnalysisCoverage::Complete && r.countSeverity(Severity::Fatal) > 0)
            errs.push_back("COMPLETE coverage despite FATAL");
        if (errs.empty()) {
            std::cout << "[ OK ] " << fs::relative(f, argv[1]).string() << '\n';
        } else {
            failed++;
            std::cout << "[FAIL] " << fs::relative(f, argv[1]).string() << '\n';
            for (auto& e : errs) std::cout << "    " << e << '\n';
            std::cout << r.toText(false, 0);
        }
    }
    std::cout << total << " fixtures, " << failed << " failed\n";
    return failed ? 1 : 0;
}
