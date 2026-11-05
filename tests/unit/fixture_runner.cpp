// Fixture-driven conformance runner. Each fixture is a SystemVerilog file annotated with comments:
//   // sverity-args: <extra slang args, e.g. --top top>
//   // expect: RULE-ID SEVERITY [line]        (a finding must exist)
//   // expect-none: RULE-ID [SEVERITY]        (no finding of that rule (and severity) may exist: "good" case)
//   // expect-coverage: <scope> INCOMPLETE    (scope must have exactly that coverage value)
//   // expect-count: RULE-ID SEVERITY N       (total occurrence count of that rule+severity must equal N)
//   // revert: <old text> => <new text>       (mutation pair: the file with this edit applied is the defect-free variant)
//   // revert-expect-none: RULE-ID [SEVERITY] (that finding must be absent from the reverted variant)
// In sverity-args, @DIR@ expands to the fixture's directory and @FIXTURES@ to the fixtures root (shared files live in
// fixtures/_common). Usage: fixture_runner <fixtures dir> [rule directory filter, e.g. CDC-003].
// Fixture directories are named after the rule they exercise: fixtures/<RULE-ID>/{good,bad,boundary,hierarchy_unknown,mut}_*.sv
// Report-integrity invariants are checked for every fixture.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

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
        std::vector<std::string> expects, nones, covs, counts, args, reverts, revNones;
        for (std::string line; std::getline(in, line);) {
            auto grab = [&](const char* key, std::vector<std::string>* out) {
                auto p = line.find(key);
                if (p != std::string::npos) out->push_back(line.substr(p + std::string(key).size()));
            };
            grab("// expect: ", &expects); grab("// expect-none: ", &nones); grab("// expect-coverage: ", &covs); grab("// expect-count: ", &counts);
            grab("// revert: ", &reverts); grab("// revert-expect-none: ", &revNones);
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
            else if (a == "--profile" && i + 1 < args.size()) o.config.profile = args[++i];
            else if (a == "--library" && i + 1 < args.size()) o.libraryFiles.push_back(args[++i]);
            else if (a == "--disable-rule" && i + 1 < args.size()) o.config.disabledRules.insert(args[++i]);
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
        std::set<std::string> fingerprints;
        for (const auto& d : r.diagnostics())
            if (!fingerprints.insert(d.fingerprint).second)
                errs.push_back("duplicate finding fingerprint: " + d.ruleId + " " + d.fingerprint);
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
        // ---- mutation pair: reverting the injected defect must clear the finding
        if (!reverts.empty() || !revNones.empty()) {
            std::ifstream in2(f);
            std::stringstream ss;
            ss << in2.rdbuf();
            std::string text;
            for (std::string line; std::getline(ss, line);)
                if (line.rfind("// revert", 0) != 0) text += line + "\n";   // the annotations themselves must not be edited
            for (auto& rv : reverts) {
                auto p = rv.find(" => ");
                if (p == std::string::npos) { errs.push_back("bad revert line: " + rv); continue; }
                std::string from = rv.substr(0, p), to = rv.substr(p + 4);
                while (!to.empty() && (to.back() == '\r' || to.back() == ' ')) to.pop_back();
                bool all = from.rfind("ALL ", 0) == 0;       // "ALL <old> => <new>" replaces every occurrence
                if (all) from = from.substr(4);
                size_t at = text.find(from);
                if (at == std::string::npos) { errs.push_back("revert text not found: " + from); continue; }
                do { text.replace(at, from.size(), to); at = all ? text.find(from, at + to.size()) : std::string::npos; } while (at != std::string::npos);
            }
            if (revNones.empty()) errs.push_back("revert without revert-expect-none");
            fs::path tmp = f.parent_path() / ("__revert_" + f.filename().string());
            { std::ofstream out(tmp); out << text; }
            AnalyzerOptions o2 = o;
            o2.frontend.slangArgs.back() = tmp.string();
            Report r2 = analyze(o2);
            fs::remove(tmp);
            for (auto& n : revNones) {
                auto w = split(n);
                if (w.empty()) continue;
                for (auto& d : r2.diagnostics())
                    if (d.ruleId == w[0] && (w.size() < 2 || w[1] == toString(d.severity))) { errs.push_back("finding survives the revert: " + n); break; }
            }
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
