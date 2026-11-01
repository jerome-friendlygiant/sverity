// End-to-end checks of the sverity binary: exit-code bit mask, usage errors, JSON/SARIF output integrity.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "sverity/json.h"
#include "testing.h"

namespace fs = std::filesystem;

namespace {
fs::path scratch() {
    static fs::path d = [] {
        auto p = fs::temp_directory_path() / ("sverity_cli_test_" + std::to_string(std::rand()));
        fs::create_directories(p);
        return p;
    }();
    return d;
}
std::string write(const std::string& name, const std::string& text) {
    auto p = scratch() / name;
    std::ofstream(p) << text;
    return p.string();
}
/// Runs sverity with `args`; returns the process exit code.
int run(const std::string& args) {
    std::string cmd = std::string(SVERITY_BIN) + " --no-color " + args + " >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}
std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
const char* kClean = "module top(input logic a, output logic y); assign y = ~a; endmodule\n";
} // namespace

TEST(cli_usage_errors_exit_64) {
    CHECK_EQ(run(""), 64);                                    // no sources
    CHECK_EQ(run("--exit-policy bogus x.sv"), 64);
    CHECK_EQ(run("--date 2026-02-30 x.sv"), 64);
    CHECK_EQ(run("--max-console 5 x.sv"), 64);                // truncation needs a complete report on disk
    CHECK_EQ(run("--json - --sarif - x.sv"), 64);
}
TEST(cli_exit_code_is_a_bit_mask_and_never_green_while_rules_are_missing) {
    auto sv = write("clean.sv", kClean);
    int strict = run("--top top " + sv);
    CHECK((strict & 4) != 0);          // analysis INCOMPLETE: catalog rules are unimplemented/partial
    CHECK((strict & 2) != 0);          // CST-019 warning needs review
    CHECK((strict & 1) == 0);          // no violations
    CHECK_EQ(run("--top top --exit-policy violations " + sv), 0);
}
TEST(cli_unreadable_input_sets_execution_error_bit) {
    int rc = run("--top top " + (scratch() / "does_not_exist.sv").string());
    CHECK((rc & 8) != 0);
}
TEST(cli_syntax_error_fails_review_and_execution) {
    auto sv = write("bad.sv", "module top(input a, output y)\n assign y = a;\nendmodule\n");
    int rc = run("--top top " + sv);
    CHECK((rc & 1) != 0);
    CHECK((rc & 8) != 0);
}
TEST(cli_json_and_sarif_are_valid_and_complete) {
    auto sv = write("clean2.sv", kClean);
    auto js = (scratch() / "r.json").string(), sa = (scratch() / "r.sarif").string();
    run("--top top --json " + js + " --sarif " + sa + " " + sv);
    auto j = sverity::json::parse(slurp(js));
    CHECK(j.ok);
    CHECK(j.value["summary"]["execution_status"].isString());
    CHECK(j.value["summary"]["analysis_coverage"].isString());
    CHECK(j.value["summary"]["review_status"].isString());
    CHECK_EQ(j.value["coverage"]["rules"].asArray().size(), 184u);      // every catalog rule is accounted for
    auto s = sverity::json::parse(slurp(sa));
    CHECK(s.ok);
    CHECK_EQ(s.value["version"].asString(), std::string("2.1.0"));
    CHECK_EQ(s.value["runs"].asArray()[0]["results"].asArray().size(), j.value["diagnostics"].asArray().size());
}
TEST(cli_output_is_deterministic) {
    auto sv = write("det.sv", kClean);
    auto a = (scratch() / "a.json").string(), b = (scratch() / "b.json").string();
    run("--top top --root " + scratch().string() + " --json " + a + " " + sv);
    run("--top top --root " + scratch().string() + " --json " + b + " " + sv);
    CHECK(!slurp(a).empty());
    CHECK_EQ(slurp(a), slurp(b));
}
