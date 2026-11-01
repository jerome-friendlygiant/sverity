// sverity CLI. SVerity-specific options are consumed here; everything else is forwarded to slang's
// driver (files, -f filelists, -D, -I, -y/-v libraries, --top, -G param overrides, ...).
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "sverity/analyzer.h"
#include "sverity/waiver.h"

using namespace sverity;

static const char* kUsage = R"(sverity - native SystemVerilog lint / CDC / RDC analyzer (slang frontend)

usage: sverity [sverity options] [slang options] <source files...>

sverity options:
  --json <file|->          write the canonical JSON report ('-' = stdout)
  --sarif <file>           write a SARIF 2.1.0 report
  --profile <name>         generic (default) | asic | fpga
  --library <file>         technology-neutral library cell description (JSON): modules treated as synchronisers
  --sdc <file>             SDC-subset constraints (repeatable): create_clock, create_generated_clock,
                           set_clock_groups, false-path/multicycle/min/max-delay; others are disclosed
  --waivers <file>         waiver file (repeatable); waived findings stay in every report
  --disable-rule <ID>      disable a rule (repeatable); it stays listed as DISABLED
  --exit-policy <p>        strict (default): non-zero for FAIL, REVIEW_REQUIRED, INCOMPLETE, ERROR
                           violations: non-zero only for FAIL or execution ERROR
  --date <YYYY-MM-DD>      date used for waiver expiry (default: today, UTC)
  --root <dir>             report paths relative to <dir> (default: current directory); makes
                           fingerprints independent of where sverity is run from
  --max-console <N>        show at most N diagnostics on the console (announced; requires --json or
                           --sarif so a complete report always exists)
  --no-color               disable ANSI colors
  --slang-default-warnings use slang's default warning set instead of -Weverything (recorded as a gap)
  --list-rules             print the rule catalog with implementation state and exit
  --version, -h, --help

Everything else (e.g. -f files.f, -DNAME=1, -I dir, --top top, -G W=8, -y libdir) goes to slang.

exit code (bit mask):
  1 review_status FAIL   2 REVIEW_REQUIRED   4 analysis INCOMPLETE   8 execution ERROR   64 usage error
)";

int main(int argc, char** argv) {
    AnalyzerOptions opts;
    std::string jsonOut, sarifOut;
    bool color = isatty(STDOUT_FILENO), strict = true, listRules = false;
    size_t maxConsole = 0;
    std::string rootDir;

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
        else if (a == "--json") jsonOut = need(i, "--json");
        else if (a == "--sarif") sarifOut = need(i, "--sarif");
        else if (a == "--profile") opts.config.profile = need(i, "--profile");
        else if (a == "--sdc") opts.sdcFiles.push_back(need(i, "--sdc"));
        else if (a == "--library") opts.libraryFiles.push_back(need(i, "--library"));
        else if (a == "--waivers") opts.waiverFiles.push_back(need(i, "--waivers"));
        else if (a == "--disable-rule") opts.config.disabledRules.insert(need(i, "--disable-rule"));
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
        else if (a == "--no-color") color = false;
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

    if (jsonOut == "-" && sarifOut == "-") {
        std::fputs("sverity: --json - and --sarif - would write two documents to stdout\n", stderr);
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
        if (path == "-") { std::fwrite(text.data(), 1, text.size(), stdout); return true; }
        std::ofstream f(path, std::ios::binary);
        if (!f) { std::fprintf(stderr, "sverity: cannot write %s\n", path.c_str()); return false; }
        f << text;
        f.flush();
        if (!f) { std::fprintf(stderr, "sverity: write error on %s\n", path.c_str()); return false; }
        return true;
    };
    int extra = 0;
    if (!jsonOut.empty() && !writeTo(jsonOut, report.toJson().dump(2) + "\n")) extra = 8;
    if (!sarifOut.empty() && !writeTo(sarifOut, report.toSarif().dump(2) + "\n")) extra = 8;

    std::string hint = !jsonOut.empty() && jsonOut != "-" ? jsonOut : !sarifOut.empty() && sarifOut != "-" ? sarifOut : jsonOut == "-" ? "the JSON report on stdout" : "";
    std::string text = report.toText(color, maxConsole, hint);
    std::fputs(text.c_str(), jsonOut == "-" ? stderr : stdout);
    return report.exitCode(strict) | extra;
}
