// sverity CLI. SVerity-specific options are consumed here; everything else is forwarded to slang's
// driver (files, -f filelists, -D, -I, -y/-v libraries, --top, -G param overrides, ...).
#include <unistd.h>
#include <fnmatch.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <climits>
#include <charconv>
#include <regex>
#include <sstream>

#include "sverity/analyzer.h"
#include "sverity/waiver.h"

using namespace sverity;

// CST goal membership: completeness/disclosure and waiver/report integrity
// apply to all goals; clock, crossing and reset contracts apply to the
// analyses they describe. The full table is explicit so an added CST rule
// cannot accidentally disappear from a scoped run.
static bool inGoal(const std::string& id, const std::set<std::string>& goals) {
    if (goals.empty()) return true;
    if (id.rfind("LNT-", 0) == 0) return goals.count("lint");
    if (id.rfind("CDC-", 0) == 0) return goals.count("cdc");
    if (id.rfind("RDC-", 0) == 0) return goals.count("rdc");
    // Bits: 1 lint, 2 CDC, 4 RDC.
    static const std::map<std::string, unsigned> cst = {
        {"CST-001",6}, // clock and reset classification
        {"CST-002",6}, // constraint object resolution
        {"CST-003",6}, // clock and exception conflicts
        {"CST-004",6}, // crossing clock relationship
        {"CST-005",7}, // black-box inventory
        {"CST-006",7}, // waiver validity
        {"CST-007",6}, // synchronizer trust models
        {"CST-008",6}, // timing exceptions and crossing safety
        {"CST-009",7}, // constraint parser disclosure
        {"CST-010",7}, // unsupported analysis disclosure
        {"CST-011",6}, // sequential clock constraint coverage
        {"CST-012",2}, // IO and IP CDC contracts
        {"CST-013",6}, // generated clock consistency
        {"CST-014",6}, // clock modes and exclusivity
        {"CST-015",7}, // configuration completeness
        {"CST-016",4}, // reset assumptions
        {"CST-017",6}, // black-box protocol and reset metadata
        {"CST-018",7}, // constraint parser completeness
        {"CST-019",7}, // rule coverage and accounting
        {"CST-020",6}, // crossing inventory completeness
        {"CST-021",6}, // clock and reset provenance
        {"CST-022",7}, // waiver hygiene
        {"CST-023",7}, // unmodeled and encrypted coverage
        {"CST-024",6}, // physical CDC/RDC obligations
    };
    auto it = cst.find(id);
    if (it == cst.end()) return true; // unclassified future families remain visible
    unsigned selected = (goals.count("lint") ? 1u : 0u) | (goals.count("cdc") ? 2u : 0u) | (goals.count("rdc") ? 4u : 0u);
    return (it->second & selected) != 0;
}

static const char* kUsage = R"(sverity - native SystemVerilog lint / CDC / RDC analyzer (slang frontend)

usage: sverity [sverity options] [slang options] <source files...>

Default console: outcome first, then every finding in severity order; INFO is one line.
Use --verbose for full detail; --group requires a complete saved report.

sverity options:
  --json <file|->          write the canonical JSON report ('-' = stdout)
  --jsonl <file|->         write finalized JSON Lines (no completion record = incomplete stream)
  --sarif <file|->         write a SARIF 2.1.0 report ('-' = stdout)
  --html <file>            write a self-contained HTML report
  --profile <name>         generic (default) | asic | fpga
  --library <file>         technology-neutral library cell description (JSON): modules treated as synchronisers
  --sdc <file>             SDC-subset constraints (repeatable): create_clock, create_generated_clock,
                           set_clock_groups, false-path/multicycle/min/max-delay; others are disclosed
  --no-source-waivers     ignore source waiver comments (recorded in inputs)
  --waiver-template <file> write open findings with rejected owner/reason placeholders
  --waivers <file>         waiver file (repeatable); waived findings stay in every report
  --config <file>          JSON configuration; paths are relative to the file
  --goal <lint|cdc|rdc>    limit rule families (repeatable; union); CLI goals replace config goals
                           goal, file enable/disable, then CLI enable/disable; disabled coverage stays INCOMPLETE
  --option <key=value>     override a rule option from the file (repeatable)
  --severity <pattern=level> override severity for matching rules (repeatable)
  --enable-rule <pattern>  enable matching rules (repeatable; narrows file enable list)
  --disable-rule <pattern> disable matching rules (repeatable); they stay listed as DISABLED
                           CLI settings win; within each layer disable wins over enable
  --exit-policy <p>        strict (default): non-zero for FAIL, REVIEW_REQUIRED, INCOMPLETE, ERROR
                           violations: non-zero only for FAIL or execution ERROR
  --date <YYYY-MM-DD>      date used for waiver expiry (default: today, UTC)
  --root <dir>             report paths relative to <dir> (default: current directory); makes
                           fingerprints independent of where sverity is run from
  --verbose                show all evidence, occurrences, identity, proof state and run context
  --group                  group related findings in console (requires a successful JSON or SARIF report)
  --max-console <N>        show at most N diagnostics on the console (announced; requires --json or
                           --sarif so a complete report always exists)
  --color <auto|always|never> auto checks the human output TTY and non-empty NO_COLOR
  --no-color               alias for --color never; --color always overrides NO_COLOR
  --slang-default-warnings use slang's default warning set instead of -Weverything (recorded as a gap)
  --explain <RULE-ID>      explain a catalog rule without a design
  --summary                append complete per-rule accounting
  --statistics             append nonzero per-rule severity counts, then the outcome
  --list-rules             print the rule catalog with implementation state and exit
  --version, -h, --help

Machine stdout (--json -, --jsonl - or --sarif -) sends human text to stderr.
Only one machine format may use stdout. Machine data never contains ANSI colors.

Examples:
  sverity --top top rtl.sv
  sverity --top top rtl.sv --json report.json --sarif report.sarif --statistics
  sverity --top top rtl.sv --jsonl - --color never

Everything else (e.g. -f files.f, -DNAME=1, -I dir, --top top, -G W=8, -y libdir) goes to slang.

exit code (bit mask):
  1 review_status FAIL   2 REVIEW_REQUIRED   4 analysis INCOMPLETE   8 execution ERROR   64 usage error
)";

int main(int argc, char** argv) {
    AnalyzerOptions opts;
    std::string jsonOut, jsonlOut, sarifOut, htmlOut, templateOut;
    std::string colorMode = "auto";
    bool strict = true, listRules = false, groupConsole = false;
    size_t maxConsole = 0;
    bool summary = false, statistics = false, verbose = false;
    std::string rootDir, configFile;
    std::set<std::string> cliEnable, cliDisable, cliGoals;
    std::vector<std::pair<std::string, std::string>> cliOptions, cliSeverities;
    auto pairArg = [&](const std::string& text, const char* flag) {
        auto pos = text.find('=');
        if (pos == std::string::npos || pos == 0) {
            std::fprintf(stderr, "sverity: %s expects name=value\n", flag); std::exit(64);
        }
        return std::make_pair(text.substr(0, pos), text.substr(pos + 1));
    };

    auto need = [&](int& i, const char* opt) -> const char* {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "sverity: option %s needs a value\n", opt);
            std::exit(64);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { std::fputs(kUsage, stdout); return 0; }
        else if (a == "--version") {
            std::printf("sverity %s (slang %s, catalog %s)\n", SVERITY_VERSION, slangVersionString().c_str(),
                        Catalog::embedded().version().c_str());
            return 0;
        }
        else if (a == "--explain") {
            std::string id = need(i, "--explain");
            const auto* rule = Catalog::embedded().find(id);
            if (!rule) { std::fprintf(stderr, "sverity: %s is not in the catalog\n", id.c_str()); return 64; }
            std::cout << rule->id << ": " << rule->title << "\nStatus: " << rule->status << "\n";
            if (!rule->gap.empty()) std::cout << "Gap: " << rule->gap << "\n";
            std::cout << ruleHelp(id);
            return 0;
        }
        else if (a == "--statistics") statistics = true;
        else if (a == "--summary") summary = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--json") jsonOut = need(i, "--json");
        else if (a == "--jsonl") jsonlOut = need(i, "--jsonl");
        else if (a == "--sarif") sarifOut = need(i, "--sarif");
        else if (a == "--html") htmlOut = need(i, "--html");
        else if (a == "--profile") opts.config.profile = need(i, "--profile");
        else if (a == "--sdc") opts.sdcFiles.push_back(need(i, "--sdc"));
        else if (a == "--library") opts.libraryFiles.push_back(need(i, "--library"));
        else if (a == "--no-source-waivers") opts.sourceWaivers = false;
        else if (a == "--waiver-template") templateOut = need(i, "--waiver-template");
        else if (a == "--waivers") opts.waiverFiles.push_back(need(i, "--waivers"));
        else if (a == "--config") { if (!configFile.empty()) { std::fputs("sverity: --config may be specified once\n", stderr); return 64; } configFile = need(i, "--config"); }
        else if (a == "--goal") cliGoals.insert(need(i, "--goal"));
        else if (a == "--option") cliOptions.push_back(pairArg(need(i, "--option"), "--option"));
        else if (a == "--severity") cliSeverities.push_back(pairArg(need(i, "--severity"), "--severity"));
        else if (a == "--enable-rule") cliEnable.insert(need(i, "--enable-rule"));
        else if (a == "--disable-rule") cliDisable.insert(need(i, "--disable-rule"));
        else if (a == "--date") {
            opts.today = need(i, "--date");
            if (!isValidIsoDate(opts.today)) {
                std::fprintf(stderr, "sverity: --date needs a valid YYYY-MM-DD date, got '%s'\n", opts.today.c_str());
                return 64;
            }
        }
        else if (a == "--root") rootDir = need(i, "--root");
        else if (a == "--max-console") {
            std::string v = need(i, "--max-console");
            char* end = nullptr;
            unsigned long n = std::strtoul(v.c_str(), &end, 10);
            if (v.empty() || *end || n == 0) {
                std::fprintf(stderr, "sverity: --max-console needs a positive integer, got '%s'\n", v.c_str());
                return 64;
            }
            maxConsole = n;
        }
        else if (a == "--group") groupConsole = true;
        else if (a == "--no-color") colorMode = "never";
        else if (a == "--color") {
            colorMode = need(i, "--color");
            if (colorMode != "auto" && colorMode != "always" && colorMode != "never") {
                std::fputs("sverity: --color expects auto, always or never\n", stderr); return 64;
            }
        }
        else if (a == "--slang-default-warnings") opts.frontend.enableAllUpstreamWarnings = false;
        else if (a == "--list-rules") listRules = true;
        else if (a == "--exit-policy") {
            std::string p = need(i, "--exit-policy");
            if (p == "strict") strict = true;
            else if (p == "violations") strict = false;
            else { std::fprintf(stderr, "sverity: unknown exit policy '%s'\n", p.c_str()); return 64; }
        }
        else opts.frontend.slangArgs.push_back(a);
    }

    auto fail = [](const std::string& message) { std::fprintf(stderr, "sverity: %s\n", message.c_str()); std::exit(64); };
    json::Value file = json::Value::object();
    if (!configFile.empty()) {
        std::ifstream in(configFile, std::ios::binary);
        if (!in) fail("cannot read config file '" + configFile + "'");
        std::ostringstream buffer; buffer << in.rdbuf();
        auto parsed = json::parse(buffer.str());
        if (!parsed.ok) fail("invalid config JSON: " + parsed.error);
        if (!parsed.value.isObject()) fail("config must be a JSON object");
        file = std::move(parsed.value);
    }
    const std::set<std::string> keys = {"options", "severity", "enable", "disable", "sdc", "library", "waivers", "goal"};
    for (auto& [key, value] : file.asObject()) if (!keys.count(key)) fail("unknown config key '" + key + "'");
    auto array = [&](const char* key) {
        std::vector<std::string> values;
        if (auto* item = file.find(key)) {
            if (!item->isArray()) fail(std::string("config '") + key + "' must be an array of strings");
            for (auto& v : item->asArray()) {
                if (!v.isString() || v.asString().empty()) fail(std::string("config '") + key + "' must contain nonempty strings");
                values.push_back(v.asString());
            }
        }
        return values;
    };
    auto fileEnable = array("enable"), fileDisable = array("disable"), fileGoals = array("goal");
    if (file.find("goal") && fileGoals.empty()) fail("config 'goal' must contain at least one goal");
    auto validGoal = [&](const std::string& goal) {
        if (goal != "lint" && goal != "cdc" && goal != "rdc") fail("unknown goal '" + goal + "' (lint|cdc|rdc)");
    };
    for (const auto& goal : fileGoals) validGoal(goal);
    for (const auto& goal : cliGoals) validGoal(goal);
    if (!cliGoals.empty()) opts.config.goals = cliGoals;
    else opts.config.goals.insert(fileGoals.begin(), fileGoals.end());
    auto applyPaths = [&](const char* key, std::vector<std::string>& target) {
        auto items = array(key);
        if (!target.empty() || items.empty()) return;
        auto base = std::filesystem::absolute(std::filesystem::path(configFile)).parent_path();
        for (const auto& item : items) target.push_back((base / item).lexically_normal().string());
    };
    applyPaths("sdc", opts.sdcFiles); applyPaths("library", opts.libraryFiles); applyPaths("waivers", opts.waiverFiles);
    std::map<std::string, std::string> options;
    if (auto* item = file.find("options")) {
        if (!item->isObject()) fail("config 'options' must be an object");
        for (auto& [key, value] : item->asObject()) {
            if (key == "CDC-005.min_stages" || key == "RDC-006.min_stages") {
                if (!value.isInt()) fail("option '" + key + "' must be a JSON integer");
                options[key] = std::to_string(value.asInt());
            } else if (key == "LNT-036.report_modular") {
                if (!value.isBool()) fail("option '" + key + "' must be a JSON boolean");
                options[key] = value.asBool() ? "true" : "false";
            } else if (key == "LNT-061.allow" || key == "LNT-025.priority_chains") {
                if (!value.isString()) fail("option '" + key + "' must be a JSON string");
                options[key] = value.asString();
            } else fail("unknown rule option '" + key + "'");
        }
    }
    auto validateOption = [&](const std::string& key, const std::string& value) {
        if (key == "CDC-005.min_stages" || key == "RDC-006.min_stages") {
            int n = 0; auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (value.empty() || error != std::errc{} || end != value.data() + value.size() || n < 2)
                fail("option '" + key + "' must be an integer between 2 and " + std::to_string(INT_MAX));
        } else if (key == "LNT-025.priority_chains") {
            if (value != "info" && value != "warning") fail("option '" + key + "' must be info or warning");
        } else if (key == "LNT-036.report_modular") {
            if (value != "true" && value != "false") fail("option '" + key + "' must be true or false");
        } else if (key == "LNT-061.allow") {
            if (!value.empty() && !std::regex_match(value, std::regex(R"(\s*\$?[A-Za-z_][A-Za-z0-9_$]*(\s*,\s*\$?[A-Za-z_][A-Za-z0-9_$]*)*\s*)")))
                fail("option '" + key + "' must be a comma-separated list of system task names");
        } else fail("unknown rule option '" + key + "'");
    };
    for (auto& [key, value] : options) validateOption(key, value);
    for (auto& [key, value] : cliOptions) { validateOption(key, value); options[key] = value; }
    opts.config.ruleOptions = std::move(options);
    auto expand = [&](const std::string& pattern) {
        std::set<std::string> ids;
        for (const auto& rule : Catalog::embedded().rules())
            if (fnmatch(pattern.c_str(), rule.id.c_str(), 0) == 0) ids.insert(rule.id);
        if (ids.empty()) fail("rule pattern '" + pattern + "' matches no rules");
        return ids;
    };
    for (const auto& rule : Catalog::embedded().rules())
        if (!inGoal(rule.id, opts.config.goals)) opts.config.disabledRules.insert(rule.id);
    // An explicit file enable list narrows the default set and can restore
    // a rule excluded by the goal. File disable wins
    // within that layer; CLI enables can restore it and CLI disables win last.
    if (file.find("enable")) {
        std::set<std::string> selected;
        for (auto& pattern : fileEnable) { auto ids = expand(pattern); selected.insert(ids.begin(), ids.end()); }
        for (auto& rule : Catalog::embedded().rules()) {
            if (selected.count(rule.id)) opts.config.disabledRules.erase(rule.id);
            else opts.config.disabledRules.insert(rule.id);
        }
    }
    for (auto& pattern : fileDisable) for (auto& id : expand(pattern)) opts.config.disabledRules.insert(id);
    for (auto& pattern : cliEnable) for (auto& id : expand(pattern)) opts.config.disabledRules.erase(id);
    for (auto& pattern : cliDisable) for (auto& id : expand(pattern)) opts.config.disabledRules.insert(id);
    for (const auto& rule : Catalog::embedded().rules())
        if (!inGoal(rule.id, opts.config.goals) && !opts.config.disabledRules.count(rule.id)) opts.config.goalBroadened = true;
    auto severity = [&](const std::string& pattern, const std::string& level) {
        auto sev = severityFromString(level);
        if (!sev || *sev == Severity::Fatal) fail("severity for '" + pattern + "' must be INFO, WARNING or ERROR");
        for (auto& id : expand(pattern)) opts.config.severityOverrides[id] = *sev;
    };
    if (auto* item = file.find("severity")) {
        if (!item->isObject()) fail("config 'severity' must be an object");
        for (auto& [pattern, value] : item->asObject()) {
            if (!value.isString()) fail("severity for '" + pattern + "' must be a string");
            severity(pattern, value.asString());
        }
    }
    for (auto& [pattern, value] : cliSeverities) severity(pattern, value);

    if (listRules) {
        auto rules = makeAllRules();
        std::map<std::string, RuleState> st;
        for (auto& r : rules) st[std::string(r->id())] = r->completeness();
        for (auto& ri : Catalog::embedded().rules()) {
            auto it = st.find(ri.id);
            std::printf("%-8s %-13s %-12s %s\n", ri.id.c_str(), it == st.end() ? "UNIMPLEMENTED" : toString(it->second),
                        ri.wave.c_str(), ri.title.c_str());
        }
        return 0;
    }
    if (opts.config.profile != "generic" && opts.config.profile != "asic" && opts.config.profile != "fpga") {
        std::fprintf(stderr, "sverity: unknown profile '%s' (generic|asic|fpga)\n", opts.config.profile.c_str());
        return 64;
    }
    if (opts.frontend.slangArgs.empty()) {
        std::fputs(kUsage, stderr);
        return 64;
    }

    if (htmlOut == "-") {
        std::fputs("sverity: --html requires a file path\n", stderr);
        return 64;
    }
    if (int(jsonOut == "-") + int(jsonlOut == "-") + int(sarifOut == "-") > 1) {
        std::fputs("sverity: only one of --json, --jsonl and --sarif may write stdout\n", stderr);
        return 64;
    }
    if (maxConsole && jsonOut.empty() && sarifOut.empty()) {
        std::fputs("sverity: --max-console needs --json or --sarif so the complete report is not lost\n", stderr);
        return 64;
    }
    std::error_code ec;
    opts.pathRoot = rootDir.empty() ? std::filesystem::current_path(ec).generic_string()
                                    : std::filesystem::absolute(rootDir, ec).lexically_normal().generic_string();
    Report report = analyze(opts);

    auto writeTo = [&](const std::string& path, const std::string& text) {
        if (path == "-") {
            bool ok = std::fwrite(text.data(), 1, text.size(), stdout) == text.size() && std::fflush(stdout) == 0;
            if (!ok) std::fputs("sverity: write error on stdout\n", stderr);
            return ok;
        }
        std::ofstream f(path, std::ios::binary);
        if (!f) { std::fprintf(stderr, "sverity: cannot write %s\n", path.c_str()); return false; }
        f << text;
        f.flush();
        if (!f) { std::fprintf(stderr, "sverity: write error on %s\n", path.c_str()); return false; }
        return true;
    };
    int extra = 0;
    if (!templateOut.empty() && !writeTo(templateOut, waiverTemplate(report).dump(2) + "\n")) extra = 8;
    bool jsonWritten = false, sarifWritten = false;
    // Files first, stdout last and once: a failed write sets the execution error, then the files already
    // written are rewritten, so no saved report claims a success the process does not.
    const auto reportFor = [&](const std::string& kind) -> std::string {
        if (kind == "json") return report.toJson().dump(2) + "\n";
        if (kind == "sarif") return report.toSarif().dump(2) + "\n";
        if (kind == "html") return report.toHtml();
        return report.toJsonl(strict, extra);
    };
    const std::vector<std::pair<std::string, std::string>> outputs = {{"json", jsonOut}, {"sarif", sarifOut}, {"html", htmlOut}, {"jsonl", jsonlOut}};
    const auto failedWrite = [&] { extra = 8; report.setExecutionError("a report could not be written"); };
    if (extra & 8) failedWrite();
    std::map<std::string, bool> written;
    const auto emit = [&](const std::string& kind, const std::string& path) {
        written[kind] = writeTo(path, reportFor(kind));
        if (!written[kind]) failedWrite();
    };
    for (const auto& [kind, path] : outputs) if (!path.empty() && path != "-") emit(kind, path);
    for (const auto& [kind, path] : outputs) if (path == "-") emit(kind, path);
    if (extra & 8)
        for (const auto& [kind, path] : outputs)
            if (!path.empty() && path != "-" && written[kind]) writeTo(path, reportFor(kind));
    jsonWritten = written["json"]; sarifWritten = written["sarif"];
    const bool machineStdout = jsonOut == "-" || jsonlOut == "-" || sarifOut == "-";
    const char* noColor = std::getenv("NO_COLOR");
    const bool color = colorMode == "always" || (colorMode == "auto" && !(noColor && *noColor) &&
        isatty(machineStdout ? STDERR_FILENO : STDOUT_FILENO));
    bool completeWritten = jsonWritten || sarifWritten;
    if (groupConsole && !completeWritten) std::fputs("sverity: --group requires a written JSON or SARIF report; showing the complete console\n", stderr);
    std::string hint = jsonWritten ? (jsonOut == "-" ? "the JSON report on stdout" : jsonOut) : sarifWritten ? (sarifOut == "-" ? "the SARIF report on stdout" : sarifOut) : "";
    std::string text = report.toText(color, completeWritten ? maxConsole : 0, hint, groupConsole && completeWritten, verbose, strict, extra, statistics);
    if (summary) text += report.ruleSummary();
    std::fputs(text.c_str(), machineStdout ? stderr : stdout);
    return report.exitCode(strict) | extra;
}
