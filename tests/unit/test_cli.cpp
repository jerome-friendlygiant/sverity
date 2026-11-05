// End-to-end checks of the sverity binary: exit-code bit mask, usage errors, JSON/SARIF output integrity.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

#include "sverity/catalog.h"
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
    CHECK_EQ(j.value["coverage"]["rules"].asArray().size(), sverity::Catalog::embedded().rules().size());      // every catalog rule is accounted for
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

TEST(cli_fingerprints_are_independent_of_checkout_root) {
    auto a = scratch() / "checkout_a", b = scratch() / "checkout_b";
    fs::create_directories(a); fs::create_directories(b);
    const char* source = "module top(input logic a, output logic q); logic s; assign s = 1'bx; always_comb begin q = a; if (s) q = 1'b0; end endmodule\n";
    std::ofstream(a / "x.sv") << source; std::ofstream(b / "x.sv") << source;
    for (const auto& dir : {a, b})
        run("--top top --root " + dir.string() + " --json " + (dir / "out.json").string() + " " + (dir / "x.sv").string());
    auto left = sverity::json::parse(slurp((a / "out.json").string()));
    auto right = sverity::json::parse(slurp((b / "out.json").string()));
    CHECK(left.ok); CHECK(right.ok);
    std::string lf, rf;
    for (const auto& d : left.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "LNT-066") { lf = d["fingerprint"].asString(); CHECK(d["message"].asString().find(a.string()) == std::string::npos); }
    for (const auto& d : right.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "LNT-066") rf = d["fingerprint"].asString();
    CHECK(!lf.empty()); CHECK_EQ(lf, rf);
}

TEST(cli_disable_rule_globs_are_expanded_and_unmatched_rejected) {
    auto sv = write("glob.sv", kClean);
    auto out = (scratch() / "glob.json").string();
    CHECK_EQ(run("--disable-rule 'NO-SUCH-*' --top top " + sv), 64);
    run("--disable-rule 'CDC-*' --top top --json " + out + " " + sv);
    auto result = sverity::json::parse(slurp(out)); CHECK(result.ok);
    size_t found = 0;
    for (const auto& rule : result.value["coverage"]["rules"].asArray()) {
        if (rule["rule_id"].asString().rfind("CDC-", 0) == 0) { CHECK_EQ(rule["state"].asString(), std::string("DISABLED")); found++; }
    }
    CHECK(found > 0);
}

TEST(cli_cdc_messages_describe_captures_and_comparable_periods) {
    std::string fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    auto out = (scratch() / "cdc-labels.json").string();
    auto check = [&](const char* file, const char* rule, const char* expected) {
        run("--top top --sdc " + fixtures + "_common/async.sdc --json " + out + " " + fixtures + file);
        auto result = sverity::json::parse(slurp(out)); CHECK(result.ok); bool found = false;
        for (const auto& d : result.value["diagnostics"].asArray())
            if (d["rule_id"].asString() == rule && d["message"].asString().find(expected) != std::string::npos) found = true;
        CHECK(found);
    };
    check("CDC-032/bad_scalar_fanout_depth.sv", "CDC-032", "captures of");
    check("CDC-032/bad_scalar_fanout_depth.sv", "CDC-009", "synchroniser captures");
    check("CDC-013/bad_comparable_periods.sv", "CDC-013", "comparable to or shorter");
}

namespace {
std::string cfgRun(const std::string& config, const std::string& extra = "", const std::string& source = kClean) {
    auto f = write("config-check.json", config), sv = write("config-check.sv", source);
    auto out = (scratch() / "config-result.json").string();
    run("--top top --root " + scratch().string() + " --config " + f + " --json " + out + " " + extra + " " + sv);
    return out;
}
}
TEST(cli_config_rule_option_applies_and_is_recorded) {
    auto out = cfgRun(R"({"options":{"CDC-005.min_stages":3}})");
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    CHECK_EQ(j.value["inputs"]["rule_options"]["CDC-005.min_stages"].asString(), std::string("3"));
    auto source = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-005/bad_three_required.sv";
    auto sdc = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc";
    auto config = write("depth.json", R"({"options":{"CDC-005.min_stages":3}})");
    run("--top top --sdc " + sdc + " --config " + config + " --json " + out + " " + source);
    j = sverity::json::parse(slurp(out)); bool found = false;
    for (const auto& d : j.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-005" && d["severity"].asString() == "ERROR") found = true;
    CHECK(found);
}
TEST(cli_config_unknown_option_rejected) { auto f=write("bad-option.json", R"({"options":{"CDC-005.typo":3}})"); CHECK_EQ(run("--config " + f + " x.sv"), 64); }
TEST(cli_config_invalid_option_rejected) {
    for (const char* value : {"1", "3.5", "\"3garbage\"", "\"3\"", "true"}) {
        auto f=write("bad-value.json", std::string("{\"options\":{\"RDC-006.min_stages\":")+value+"}}");
        CHECK_EQ(run("--config " + f + " x.sv"), 64);
    }
}
TEST(cli_config_unknown_key_and_json_rejected) {
    for (const char* text : {"{\"typo\":1}", "{", "{\"disable\":123}"}) {
        auto f=write("bad-config.json", text); CHECK_EQ(run("--config " + f + " x.sv"), 64);
    }
}
TEST(cli_config_unmatched_patterns_rejected) {
    for (const char* text : {R"({"enable":["NO-*"]})",R"({"disable":["NO-*"]})",R"({"severity":{"NO-*":"INFO"}})"}) {
        auto f=write("bad-pattern.json", text); CHECK_EQ(run("--config " + f + " x.sv"), 64);
    }
}
TEST(cli_config_cli_precedence_and_enable_disable) {
    auto out=cfgRun(R"({"options":{"CDC-005.min_stages":4},"disable":["CDC-*"]})", "--enable-rule CDC-005 --disable-rule CDC-006 --option CDC-005.min_stages=3");
    auto j=sverity::json::parse(slurp(out)); CHECK(j.ok); CHECK_EQ(j.value["inputs"]["rule_options"]["CDC-005.min_stages"].asString(),std::string("3"));
    for (const auto& r:j.value["coverage"]["rules"].asArray()) {
        if (r["rule_id"].asString()=="CDC-005") CHECK(r["state"].asString()!="DISABLED");
        if (r["rule_id"].asString()=="CDC-006") CHECK_EQ(r["state"].asString(),std::string("DISABLED"));
    }
}
TEST(cli_config_override_retains_identity_and_original_severity) {
    auto source=std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/CDC-005/bad_three_required.sv";
    auto sdc=std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/_common/async.sdc";
    auto a=(scratch()/"severity-before.json").string(), b=(scratch()/"severity-after.json").string(), sarif=(scratch()/"severity.sarif").string();
    auto common="--top top --sdc "+sdc+" --option CDC-005.min_stages=3 ";
    run(common+"--json "+a+" "+source);
    auto f=write("severity.json", R"({"severity":{"CDC-005":"WARNING"}})");
    int strict = run(common+"--config "+f+" --json "+b+" --sarif "+sarif+" "+source);
    CHECK((strict & 2) != 0 && (strict & 1) == 0);
    CHECK_EQ(run(common+"--exit-policy violations --config "+f+" --json "+b+" "+source), 0);
    auto x=sverity::json::parse(slurp(a)), y=sverity::json::parse(slurp(b)), z=sverity::json::parse(slurp(sarif));
    CHECK(x.ok && y.ok && z.ok); std::string before, after; bool original=false, sarifOriginal=false;
    for (const auto& d:x.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CDC-005") before=d["fingerprint"].asString();
    for (const auto& d:y.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CDC-005") { after=d["fingerprint"].asString(); original=d["severity"].asString()=="WARNING" && d["original_severity"].asString()=="ERROR"; }
    for (const auto& d:z.value["runs"].asArray()[0]["results"].asArray()) if(d["ruleId"].asString()=="CDC-005") sarifOriginal=d["properties"]["originalSeverity"].asString()=="ERROR";
    CHECK(!before.empty()); CHECK_EQ(before,after); CHECK(original); CHECK(sarifOriginal);
    CHECK_EQ(z.value["runs"].asArray()[0]["properties"]["inputs"]["severity_overrides"]["CDC-005"].asString(),std::string("WARNING")); CHECK_EQ(y.value["summary"]["review_status"].asString(),std::string("REVIEW_REQUIRED"));
}
TEST(cli_config_unresolved_floor_is_visible) {
    auto out=cfgRun(R"({"severity":{"CST-019":"INFO"}})"); auto j=sverity::json::parse(slurp(out)); bool found=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CST-019") found=d["severity"].asString()=="WARNING" && d["requested_severity"].asString()=="INFO" && !d["severity_override_reason"].asString().empty();
    CHECK(found);
}
TEST(cli_config_override_never_raises_a_trusted_recognition) {
    auto sv = write("recognised.sv", "module top(input logic clka, clkb, d, output logic q);\n"
        "logic src, s1; always_ff @(posedge clka) src <= d;\n"
        "always_ff @(posedge clkb) begin s1 <= src; q <= s1; end\nendmodule\n");
    auto out = (scratch() / "recognised.json").string();
    run("--severity CDC-003=INFO --top top --sdc " + std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc --json " + out + " " + sv);
    auto report = sverity::json::parse(slurp(out)); CHECK(report.ok);
    bool found = false;
    for (const auto& d : report.value["diagnostics"].asArray())
        if (d["rule_id"].asString() == "CDC-003") found = d["severity"].asString() == "INFO" && d["original_severity"].asString() == "INFO";
    CHECK(found);
}
TEST(cli_config_cli_severity_wins_and_invalid_file_still_rejected) {
    auto out=cfgRun(R"({"severity":{"CST-019":"ERROR"}})", "--severity CST-019=INFO");
    auto j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CST-019") seen=d["requested_severity"].asString()=="INFO" && d["severity"].asString()=="WARNING";
    CHECK(seen);
    auto f=write("invalid-overridden.json", R"({"options":{"CDC-005.min_stages":1}})");
    CHECK_EQ(run("--config " + f + " --option CDC-005.min_stages=3 x.sv"),64);
}
TEST(cli_config_relative_paths_and_report_paths) {
    auto dir=scratch()/"relative-config"; fs::create_directories(dir);
    std::ofstream(dir/"clock.sdc") << "create_clock -name clka -period 10 [get_ports clka]\n";
    std::ofstream(dir/"settings.json") << R"({"sdc":["clock.sdc"]})";
    auto sv=write("relative-input.sv", "module top(input logic clka, d, output logic q); always_ff @(posedge clka) q <= d; endmodule\n");
    auto out=(scratch()/"relative-result.json").string();
    run("--top top --root "+scratch().string()+" --config "+(dir/"settings.json").string()+" --json "+out+" "+sv);
    auto text=slurp(out); auto j=sverity::json::parse(text); CHECK(j.ok); CHECK(text.find(scratch().string())==std::string::npos);
    for(const auto& d:j.value["diagnostics"].asArray()) CHECK(!(d["rule_id"].asString()=="CST-018" && d["severity"].asString()=="ERROR"));
}
TEST(cli_config_fatal_and_slang_errors_cannot_be_downgraded) {
    auto sv=write("fatal-config.sv", "module top(input a; endmodule\n");
    auto out=(scratch()/"fatal-config.json").string(); auto f=write("fatal-settings.json",R"({"severity":{"LNT-001":"INFO"}})");
    int rc=run("--top top --config "+f+" --json "+out+" "+sv); CHECK((rc&8)!=0);
    auto j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="LNT-001" && d["original_severity"].asString()=="FATAL") seen=d["severity"].asString()=="FATAL" && !d["severity_override_reason"].asString().empty();
    CHECK(seen);
}
TEST(cli_config_enable_cannot_implement_proposed_rule) {
    auto out=cfgRun(R"({"enable":["CST-003"],"disable":["CST-003"]})", "--enable-rule CST-003");
    auto j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& r:j.value["coverage"]["rules"].asArray()) if(r["rule_id"].asString()=="CST-003") { seen=true; CHECK_EQ(r["state"].asString(),std::string("UNIMPLEMENTED")); }
    CHECK(seen); CHECK_EQ(j.value["summary"]["analysis_coverage"].asString(),std::string("INCOMPLETE"));
}
TEST(cli_config_fingerprint_waiver_matches_after_override) {
    auto source=std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/CDC-005/bad_three_required.sv";
    auto sdc=std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/_common/async.sdc";
    auto out=(scratch()/"waiver-config.json").string();
    auto common="--top top --sdc "+sdc+" --option CDC-005.min_stages=3 --json "+out+" ";
    run(common+source); auto j=sverity::json::parse(slurp(out)); std::string fingerprint;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CDC-005") fingerprint=d["fingerprint"].asString();
    CHECK(!fingerprint.empty());
    auto waiver=write("config-waiver.json", "{\"waivers\":[{\"id\":\"W\",\"rule\":\"CDC-005\",\"fingerprint\":\""+fingerprint+"\",\"reason\":\"reviewed\",\"owner\":\"team\"}]}");
    auto cfg=write("config-waiver-settings.json", R"({"severity":{"CDC-005":"WARNING"}})");
    auto html=(scratch()/"waived.html").string();
    run(common+"--html "+html+" --config "+cfg+" --waivers "+waiver+" "+source);
    CHECK(slurp(html).find(fingerprint) != std::string::npos);
    CHECK(slurp(html).find("WAIVED") != std::string::npos);
    j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CDC-005") seen=d["fingerprint"].asString()==fingerprint && d["disposition"].asString()=="WAIVED" && d["original_severity"].asString()=="ERROR";
    CHECK(seen);
}

TEST(cli_config_obligation_warning_floor_is_visible) {
    auto source = "module top(input logic clk, rst_n, d, output logic q); always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 0; else q <= d; endmodule\n";
    auto out=cfgRun(R"({"severity":{"RDC-013":"INFO"}})", "", source);
    auto j=sverity::json::parse(slurp(out)); bool found=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="RDC-013")
        found=d["severity"].asString()=="WARNING" && d["proof_state"].asString()=="NOT_ATTEMPTED" && d["requested_severity"].asString()=="INFO";
    CHECK(found);
}

TEST(cli_config_option_types_are_strict) {
    for (const char* config : {R"({"options":{"LNT-061.allow":true}})", R"({"options":{"LNT-036.report_modular":"true"}})"}) {
        auto f=write("bad-option-type.json",config); CHECK_EQ(run("--config "+f+" x.sv"),64);
    }
}

TEST(cli_elaboration_task_text_is_not_rewritten_as_a_path) {
    auto sv = write("task-text.sv", "module top; $error(\"/request is not a path\"); endmodule\n");
    auto out = (scratch() / "task-text.json").string();
    run("--top top --root " + scratch().string() + " --json " + out + " " + sv);
    CHECK(slurp(out).find("/request is not a path") != std::string::npos);
}
TEST(cli_rule_accounting_is_listed_as_partial_with_its_gap) {
    auto sv = write("accounting.sv", "module top(input logic a, output logic q); assign q = a; endmodule\n");
    auto out = (scratch() / "accounting.json").string();
    run("--top top --json " + out + " " + sv);
    auto report = sverity::json::parse(slurp(out)); CHECK(report.ok);
    bool found = false;
    for (const auto& rule : report.value["coverage"]["rules"].asArray())
        if (rule["rule_id"].asString() == "CST-019") found = rule["state"].asString() == "PARTIAL" && !rule["reason"].asString().empty();
    CHECK(found);
}
TEST(cli_config_unreadable_source_report_has_no_absolute_path) {
    auto out=(scratch()/"missing-source-config.json").string();
    run("--root "+scratch().string()+" --json "+out+" "+(scratch()/"not-here.sv").string());
    auto text=slurp(out); CHECK(sverity::json::parse(text).ok); CHECK(text.find(scratch().string())==std::string::npos);
}

TEST(cli_config_report_paths_with_spaces_are_relative) {
    auto dir=scratch()/"root with spaces"; fs::create_directories(dir);
    auto out=dir/"result.json", missing=dir/"missing file.sv";
    run("--root '"+dir.string()+"' --json '"+out.string()+"' '"+missing.string()+"'");
    auto text=slurp(out.string()); CHECK(sverity::json::parse(text).ok);
    CHECK(text.find(dir.string())==std::string::npos);
    CHECK(text.find("missing file.sv")!=std::string::npos);
}

namespace {
const char* kGoalsCDC = R"(module top(input logic clka, clkb, d, output logic q1, q2);
 logic launch, a, b, c, d2;
 always_ff @(posedge clka) launch <= d;
 always_ff @(posedge clkb) begin a <= launch; b <= a; c <= launch; d2 <= c; end
 assign q1 = b; assign q2 = d2;
endmodule
)";
const char* kGoalsRDC = R"(module top(input logic clka, clkb, rst_a, rst_b, d, output logic out);
 logic source, stage1, stage2;
 always_ff @(posedge clka or posedge rst_a) if (rst_a) source <= 0; else source <= d;
 always_ff @(posedge clkb or posedge rst_b) if (rst_b) begin stage1 <= 0; stage2 <= 0; end else begin stage1 <= source; stage2 <= stage1; end
 assign out = stage2;
endmodule
)";
sverity::json::Value goalReport(const std::string& name, const std::string& source, const std::string& args) {
    auto sv=write("goal-"+name+".sv", source), out=(scratch()/("goal-"+name+".json")).string();
    run("--top top --root "+scratch().string()+" --json "+out+" "+args+" "+sv);
    auto j=sverity::json::parse(slurp(out)); CHECK(j.ok); return j.value;
}
size_t goalFindings(const sverity::json::Value& r, const std::string& prefix) {
    size_t n=0; for(const auto& d:r["diagnostics"].asArray()) if(d["rule_id"].asString().rfind(prefix,0)==0) n++; return n;
}
}
TEST(cli_goal_lint_disables_cdc_rdc_and_discloses_scope) {
    auto r=goalReport("lint",kGoalsCDC,"--goal lint");
    CHECK_EQ(goalFindings(r,"CDC-"),0u); CHECK_EQ(goalFindings(r,"RDC-"),0u);
    CHECK_EQ(r["summary"]["analysis_coverage"].asString(),std::string("INCOMPLETE"));
    CHECK_EQ(r["inputs"]["goals"].asArray()[0].asString(),std::string("lint"));
    for(const auto& x:r["coverage"]["rules"].asArray()) if(x["rule_id"].asString().rfind("CDC-",0)==0 || x["rule_id"].asString().rfind("RDC-",0)==0) CHECK_EQ(x["state"].asString(),std::string("DISABLED"));
}
TEST(cli_goal_cdc_preserves_unknown_relationship_and_crossing) {
    auto r=goalReport("cdc",kGoalsRDC,"--goal cdc");
    CHECK(goalFindings(r,"CDC-003")>0); CHECK(goalFindings(r,"CST-004")>0);
    bool unknown=false; for(const auto& d:r["diagnostics"].asArray()) if(d["rule_id"].asString()=="CST-004" && d["confidence"].asString()=="unresolved") unknown=true;
    CHECK(unknown);
}
TEST(cli_goal_rdc_preserves_all_reset_findings) {
    auto a=goalReport("rdc-same",kGoalsRDC,""); auto b=goalReport("rdc-same",kGoalsRDC,"--goal rdc");
    std::vector<std::string> x,y;
    for(const auto& d:a["diagnostics"].asArray()) if(d["rule_id"].asString().rfind("RDC-",0)==0) x.push_back(d.dump(-1));
    for(const auto& d:b["diagnostics"].asArray()) if(d["rule_id"].asString().rfind("RDC-",0)==0) y.push_back(d.dump(-1));
    CHECK(!x.empty()); CHECK_EQ(x,y); CHECK(goalFindings(b,"RDC-005")>0);
}
TEST(cli_goal_invalid_is_usage_error) {
    auto sv=write("goal-invalid.sv",kClean); CHECK_EQ(run("--goal potato "+sv),64);
    auto cfg=write("goal-invalid.json",R"({"goal":["potato"]})"); CHECK_EQ(run("--config "+cfg+" "+sv),64);
}
TEST(cli_goal_explicit_enable_can_restore_other_family) {
    auto r=goalReport("override",kGoalsCDC,"--goal lint --enable-rule CDC-003");
    CHECK(goalFindings(r,"CDC-003")>0); CHECK(r["inputs"]["goal_broadened"].asBool());
    for(const auto& x:r["coverage"]["rules"].asArray()) if(x["rule_id"].asString()=="CDC-003") CHECK(x["state"].asString()!="DISABLED");
}
TEST(cli_goal_fatal_frontend_diagnostics_survive_all_goals) {
    auto src="module top(input a; endmodule\n";
    for(const char* goal:{"lint","cdc","rdc"}) {
        auto r=goalReport("fatal",src,std::string("--goal ")+goal); bool fatal=false, slangError=false;
        for(const auto& d:r["diagnostics"].asArray()) {
            if(d["severity"].asString()=="FATAL") fatal=true;
            if(d["severity"].asString()=="ERROR" && d["origin"].asString()=="slang") slangError=true;
        }
        CHECK(fatal); CHECK(slangError); CHECK_EQ(r["summary"]["execution_status"].asString(),std::string("ERROR"));
    }
}
TEST(cli_goal_config_and_cli_order) {
    auto cfg=write("goals.json",R"({"goal":["cdc"],"enable":["LNT-001","CDC-003"],"disable":["CDC-003"]})");
    auto r=goalReport("config",kGoalsCDC,"--config "+cfg+" --enable-rule CDC-003");
    CHECK_EQ(r["inputs"]["goals"].asArray()[0].asString(),std::string("cdc"));
    for(const auto& x:r["coverage"]["rules"].asArray()) if(x["rule_id"].asString()=="CDC-003") CHECK(x["state"].asString()!="DISABLED");
    auto replacement=goalReport("replace",kGoalsCDC,"--config "+cfg+" --goal rdc --goal lint");
    CHECK_EQ(replacement["inputs"]["goals"].asArray().size(),2u);
}

TEST(cli_goal_console_discloses_limited_and_broadened_scope) {
    auto sv=write("goal-console.sv",kClean), out=(scratch()/"goal-console.txt").string();
    auto cmd=std::string(SVERITY_BIN)+" --no-color --verbose --top top --goal lint --enable-rule CDC-003 "+sv+" >"+out+" 2>&1";
    (void)std::system(cmd.c_str());
    auto text=slurp(out); CHECK(text.find("goal:              limited to lint (explicit enables broaden scope)")!=std::string::npos);
}
TEST(cli_goal_cdc_retains_slang_warning_and_disabled_disclosure) {
    auto source="module top(input logic a, output logic y); logic unused_signal; assign y=a; endmodule\n";
    auto r=goalReport("slang-warning",source,"--goal cdc"); bool warning=false, disclosed=false;
    for(const auto& d:r["diagnostics"].asArray()) if(d["rule_id"].asString()=="LNT-011" && d["origin"].asString()=="slang" && d["severity"].asString()=="WARNING") warning=true;
    for(const auto& rule:r["coverage"]["rules"].asArray()) if(rule["rule_id"].asString()=="LNT-011")
        disclosed=rule["state"].asString()=="DISABLED" && rule["reason"].asString().find("slang still produces")!=std::string::npos;
    CHECK(warning); CHECK(disclosed);
}

TEST(cli_group_without_report_shows_complete_console) {
    auto sv=write("group-no-report.sv", "module top(input a,b,c, output logic q); always_comb begin if(a) q=0; if(b) q=1; if(c) q=0; end endmodule\n");
    auto out=(scratch()/"group-no-report.txt").string();
    auto cmd=std::string(SVERITY_BIN)+" --no-color --top top --group "+sv+" >"+out+" 2>&1";
    (void)std::system(cmd.c_str()); auto text=slurp(out);
    CHECK(text.find("requires a written JSON or SARIF report; showing the complete console")!=std::string::npos);
    CHECK(text.find("Console grouped:")==std::string::npos);
    CHECK(text.find("[LNT-025]")!=std::string::npos);
}
TEST(cli_group_announcement_and_stable_root_identity_across_checkouts) {
    auto a=scratch()/"group-checkout-a",b=scratch()/"group-checkout-b";
    fs::create_directories(a/"rtl");fs::create_directories(b/"rtl");
    const char* code="module top(input a,b,c, output logic q); always_comb begin if(a) q=0; if(b) q=1; if(c) q=0; end endmodule\n";
    std::ofstream(a/"rtl/design.sv")<<code; std::ofstream(b/"rtl/design.sv")<<code;
    std::vector<std::string> roots;
    for(const auto& dir:{a,b}) {
        auto out=dir/"out.json", console=dir/"console.txt";
        auto root=dir==a ? dir : dir/"rtl";
        auto cmd=std::string(SVERITY_BIN)+" --no-color --top top --group --root "+root.string()+" --json "+out.string()+" "+(dir/"rtl/design.sv").string()+" >"+console.string()+" 2>&1";
        (void)std::system(cmd.c_str()); auto r=sverity::json::parse(slurp(out.string())); CHECK(r.ok);
        CHECK(slurp(console.string()).find("Console grouped:")!=std::string::npos);
        std::set<std::string> ids; size_t count=0;
        for(const auto& d:r.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="LNT-025") {ids.insert(d["root_cause"].asString()); count++;}
        CHECK(count>=2); CHECK_EQ(ids.size(),1u); if(!ids.empty()) roots.push_back(*ids.begin());
    }
    CHECK_EQ(roots.size(),2u); if(roots.size()==2) CHECK_EQ(roots[0],roots[1]);
}

TEST(cli_group_failed_report_write_does_not_group_or_truncate) {
    auto sv=write("group-write-failure.sv",kClean), out=(scratch()/"group-write-failure.txt").string();
    auto missing=(scratch()/"missing-directory"/"report.json").string();
    auto cmd=std::string(SVERITY_BIN)+" --no-color --top top --group --max-console 1 --json "+missing+" "+sv+" >"+out+" 2>&1";
    int rc=std::system(cmd.c_str()); CHECK(WIFEXITED(rc) && (WEXITSTATUS(rc)&8));
    auto text=slurp(out); CHECK(text.find("Console grouped:")==std::string::npos); CHECK(text.find("output truncated")==std::string::npos);
    CHECK(text.find("showing the complete console")!=std::string::npos);
}

TEST(cli_sourced_clocks_survive_nested_unsupported_command_and_paths_are_relative) {
    auto dir = scratch() / "sourced";
    fs::create_directories(dir / "sub" / "deep");
    std::ofstream(dir / "top.sv") << "module top(input logic a, b, d, output logic q); logic x; always_ff @(posedge a) x <= d; always_ff @(posedge b) q <= x; endmodule\n";
    std::ofstream(dir / "top.sdc") << "set A a\nsource sub/one.sdc\n";
    std::ofstream(dir / "sub" / "one.sdc") << "create_clock -name $A -period 10 [get_ports $A]\nsource deep/two.sdc\n";
    std::ofstream(dir / "sub" / "deep" / "two.sdc") << "create_clock -name b -period 7 [get_ports b]\nset_clock_groups -asynchronous -group $A -group b\nbogus_cmd x\n";
    auto out = (dir / "out.json").string();
    // Run from outside --root, with absolute input names as the analyzer receives them.
    run("--top top --root " + dir.string() + " --json " + out + " --sdc " + (dir / "top.sdc").string() + " " + (dir / "top.sv").string());
    auto text = slurp(out); auto j = sverity::json::parse(text); CHECK(j.ok);
    CHECK(text.find(scratch().string()) == std::string::npos);
    CHECK(text.find("/home/") == std::string::npos);
    CHECK_EQ(j.value["inputs"]["sdc_files"].asArray().size(), 3u);
    CHECK_EQ(j.value["inputs"]["sdc_files"].asArray()[2].asString(), std::string("sub/deep/two.sdc"));
    bool async = false, unsupported = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        CHECK(d["rule_id"].asString() != "CST-011");
        CHECK(d["rule_id"].asString() != "CST-004");
        for (const auto& e : d["evidence"].asArray())
            if (e["text"].asString().find("declared asynchronous") != std::string::npos) async = true;
        if (d["rule_id"].asString() == "CST-009") {
            unsupported = true;
            CHECK_EQ(d["location"]["file"].asString(), std::string("sub/deep/two.sdc"));
            CHECK_EQ(d["location"]["line"].asInt(), 3);
        }
    }
    CHECK(async); CHECK(unsupported);
}

TEST(cli_all_inputs_input_delay_includes_clock_ports_captured_as_data) {
    auto sv = write("inputs.sv", "module top(input logic clk, other, d, output logic q, c, r); always_ff @(posedge clk) begin q <= d; c <= other; end always_ff @(posedge other) r <= d; endmodule\n");
    auto sdc = write("inputs.sdc", "create_clock -name clk -period 10 [get_ports clk]\ncreate_clock -name other -period 7 [get_ports other]\nset_clock_groups -asynchronous -group clk -group other\nset_input_delay -clock clk 1 [all_inputs]\n");
    auto out = (scratch() / "inputs.json").string();
    run("--top top --sdc " + sdc + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    size_t inputs = 0; bool clockData = false, foreign = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        CHECK(d["rule_id"].asString() != "CST-018");
        if (d["rule_id"].asString() != "CDC-029") continue;
        ++inputs;
        auto msg = d["message"].asString();
        if (msg.find("input 'top.other'") != std::string::npos) { clockData = true; CHECK_EQ(d["severity"].asString(), std::string("INFO")); }
        if (msg.find("domain 'top.other'") != std::string::npos) { foreign = true; CHECK_EQ(d["severity"].asString(), std::string("WARNING")); }
        CHECK(msg.find("set_input_delay -clock clk") != std::string::npos);
    }
    CHECK_EQ(inputs, 3u); CHECK(clockData); CHECK(foreign);
}

TEST(cli_all_inputs_no_clocks_leaves_out_declared_clocks_only) {
    // `other` is a declared clock captured as data, so it has no contract; `free` is a clock nothing declares, so it stays in.
    auto sv = write("no_clocks.sv", "module top(input logic clk, other, free, d, output logic q, c, f, r); always_ff @(posedge clk) begin q <= d; c <= other; f <= free; end always_ff @(posedge free) r <= d; endmodule\n");
    auto sdc = write("no_clocks.sdc", "set_input_delay -clock clk 1 [all_inputs -no_clocks]\ncreate_clock -name clk -period 10 [get_ports clk]\ncreate_clock -name other -period 7 [get_ports other]\n");
    auto out = (scratch() / "no_clocks.json").string();
    run("--top top --sdc " + sdc + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    bool declared = false, data = false, undeclared = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CDC-029") continue;
        auto msg = d["message"].asString();
        const bool contract = msg.find("set_input_delay -clock clk") != std::string::npos;
        if (msg.find("input 'top.other'") != std::string::npos) { declared = true; CHECK(!contract); }
        if (msg.find("input 'top.d'") != std::string::npos) { data = true; CHECK(contract); }
        if (msg.find("input 'top.free'") != std::string::npos) { undeclared = true; CHECK(contract); }
    }
    CHECK(declared); CHECK(data); CHECK(undeclared);
}

TEST(cli_all_inputs_no_clocks_leaves_out_a_clock_declared_on_one_bit) {
    auto sv = write("no_clocks_bit.sv", "module top(input logic b, input logic [1:0] clk, output logic q); always_ff @(posedge b) q <= clk[0]; endmodule\n");
    auto sdc = write("no_clocks_bit.sdc", "create_clock -name b -period 7 [get_ports b]\ncreate_clock -name c -period 5 [get_ports {clk[0]}]\nset_clock_groups -asynchronous -group c -group b\nset_input_delay -clock b 1 [all_inputs -no_clocks]\n");
    auto out = (scratch() / "no_clocks_bit.json").string();
    run("--top top --sdc " + sdc + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    bool reported = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CDC-029") continue;
        reported = true;
        CHECK(d["message"].asString().find("declared synchronous") == std::string::npos);
    }
    CHECK(reported);
}

TEST(cli_tap_on_a_library_synchroniser_output_is_not_a_first_stage_tap) {
    auto sv = write("library_tap.sv", "module SYNC2(input CLK, D, output Q); endmodule\nmodule top(input logic a, b, d, output logic y, z); logic s, s2, s3; always_ff @(posedge a) s <= d; SYNC2 u(.CLK(b), .D(s), .Q(s2)); always_ff @(posedge b) s3 <= s2; assign y = s3; assign z = s2; endmodule\n");
    auto sdc = write("library_tap.sdc", "create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\nset_clock_groups -asynchronous -group a -group b\n");
    auto cells = write("library_tap.json", "{\"cells\":[{\"name\":\"SYNC2\",\"kind\":\"synchronizer\",\"clock\":\"CLK\",\"input\":\"D\",\"output\":\"Q\",\"stages\":2}]}\n");
    auto out = (scratch() / "library_tap.report.json").string();
    // At the default depth the cell's output is the synchroniser's output, so reading it is no tap at all.
    run("--top top --sdc " + sdc + " --library " + cells + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    for (const auto& d : j.value["diagnostics"].asArray()) CHECK(d["rule_id"].asString() != "CDC-007");
    // Short of a required depth of three it is a tap, on a later stage rather than the first.
    run("--top top --option CDC-005.min_stages=3 --sdc " + sdc + " --library " + cells + " --json " + out + " " + sv);
    j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    bool intermediate = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CDC-007") continue;
        CHECK(d["message"].asString().find("first-stage") == std::string::npos);
        intermediate |= d["message"].asString().find("intermediate stage") != std::string::npos;
    }
    CHECK(intermediate);
}

TEST(cli_source_lines_do_not_come_from_a_same_named_file_in_the_working_directory) {
    const auto design = scratch() / "excerpt_design", other = scratch() / "excerpt_other";
    std::filesystem::create_directories(design); std::filesystem::create_directories(other);
    { std::ofstream(design / "x.sv") << "module top(input logic a, b, output logic q);\n  logic s;\n  always_ff @(posedge a) s <= ~s;\n  always_ff @(posedge b) q <= s;\nendmodule\n"; }
    { std::ofstream unrelated(other / "x.sv"); for (int i = 0; i < 10; ++i) unrelated << "UNRELATED TEXT\n"; }
    const auto out = (scratch() / "excerpt.json").string();
    const std::string command = "cd " + other.string() + " && " + SVERITY_BIN + " --no-color --top top --root " + design.string() + " --json " + out + " " + (design / "x.sv").string() + " >/dev/null 2>&1";
    CHECK(std::system(command.c_str()) != -1);
    const auto text = slurp(out);
    CHECK(text.find("UNRELATED") == std::string::npos);
    CHECK(text.find("always_ff @(posedge b) q <= s;") != std::string::npos);
}

TEST(cli_tap_on_a_library_synchroniser_of_a_primary_input_is_reported) {
    auto sv = write("library_input_tap.sv", "module SYNC2(input logic CLK, D, output wire Q); assign Q = D; endmodule\nmodule top(input logic b, d, output logic q, tap); wire w; SYNC2 u(.CLK(b), .D(d), .Q(w)); always_ff @(posedge b) q <= w; assign tap = w; endmodule\n");
    auto sdc = write("library_input_tap.sdc", "create_clock -name b -period 7 [get_ports b]\n");
    auto cells = write("library_input_tap.json", "{\"cells\":[{\"name\":\"SYNC2\",\"kind\":\"synchronizer\",\"clock\":\"CLK\",\"input\":\"D\",\"output\":\"Q\",\"stages\":2}]}\n");
    auto out = (scratch() / "library_input_tap.report.json").string();
    run("--top top --option CDC-005.min_stages=3 --sdc " + sdc + " --library " + cells + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
    bool intermediate = false;
    for (const auto& d : j.value["diagnostics"].asArray())
        if (d["rule_id"].asString() == "CDC-007") intermediate |= d["message"].asString().find("intermediate") != std::string::npos;
    CHECK(intermediate);
}

TEST(cli_all_inputs_false_path_resolves_top_level_reset) {
    auto sv = write("inputs_reset.sv", "module top(input logic clk, rst, d, output logic q); always_ff @(posedge clk or posedge rst) if (rst) q <= 0; else q <= d; endmodule\n");
    auto sdc = write("inputs_reset.sdc", "create_clock -name clk -period 10 [get_ports clk]\nset_false_path -from [all_inputs]\n");
    auto out = (scratch() / "inputs_reset.json").string();
    run("--top top --sdc " + sdc + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out)); CHECK(j.ok); bool covered = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        CHECK(d["rule_id"].asString() != "CST-018");
        if (d["rule_id"].asString() == "RDC-027") {
            covered = true;
            CHECK(d["message"].asString().find("covers reset top.rst") != std::string::npos);
            CHECK_EQ(d["severity"].asString(), std::string("WARNING"));
        }
    }
    CHECK(covered);
}

TEST(cli_explain_known_without_design) {
    auto out = (scratch() / "explain.txt").string();
    auto cmd = std::string(SVERITY_BIN) + " --explain CDC-003 >" + out;
    CHECK_EQ(std::system(cmd.c_str()), 0);
    auto text = slurp(out);
    CHECK(text.find(sverity::Catalog::embedded().find("CDC-003")->title) != std::string::npos);
    CHECK(text.find("a single-bit crossing group") != std::string::npos);
}
TEST(cli_explain_unknown) { CHECK_EQ(run("--explain NOPE-1"), 64); }
TEST(cli_summary_matches_json_and_shows_state) {
    auto sv = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-005/bad_three_required.sv";
    auto sdc = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc";
    auto js = (scratch() / "summary.json").string(), out = (scratch() / "summary.txt").string();
    auto cmd = std::string(SVERITY_BIN) + " --no-color --summary --option CDC-005.min_stages=3 --disable-rule CDC-003 --sdc " + sdc + " --top top --json " + js + " " + sv + " >" + out;
    std::system(cmd.c_str());
    auto parsed = sverity::json::parse(slurp(js)); CHECK(parsed.ok);
    CHECK(parsed.value["summary"]["severity_counts"]["ERROR"].asInt() > 0);
    CHECK(parsed.value["summary"]["severity_counts"]["WARNING"].asInt() > 0);
    CHECK(parsed.value["summary"]["severity_counts"]["INFO"].asInt() > 0);
    auto text = slurp(out); CHECK(text.find("CDC-003 DISABLED ") != std::string::npos);
    for (const auto& rule : parsed.value["coverage"]["rules"].asArray()) {
        std::string row = rule["rule_id"].asString() + " " + rule["state"].asString();
        for (const char* key : {"FATAL", "ERROR", "WARNING", "INFO"}) row += " " + std::to_string(rule["severity_counts"][key].asInt());
        for (const char* key : {"OPEN", "REVIEW_REQUIRED", "RESOLVED", "WAIVED"}) row += " " + std::to_string(rule["disposition_counts"][key].asInt());
        CHECK(text.find(row + "\n") != std::string::npos);
    }
}
TEST(cli_embedded_specs_match_family_docs) {
    for (const auto& rule : sverity::Catalog::embedded().rules()) {
        auto doc = slurp(std::string(SVERITY_SOURCE_DIR) + "/docs/rules/" + rule.category + ".md");
        auto heading = "\n### " + rule.id + "\n";
        auto start = doc.find(heading);
        if (start == std::string::npos) continue;
        start += heading.size(); auto end = doc.find("\n##", start);
        auto block = doc.substr(start, end - start);
        auto out = (scratch() / "spec_sync.txt").string();
        auto cmd = std::string(SVERITY_BIN) + " --explain " + rule.id + " >" + out;
        CHECK_EQ(std::system(cmd.c_str()), 0);
        CHECK(slurp(out).find(block) != std::string::npos);
    }
}

TEST(cli_html_reports_all_findings_and_is_deterministic) {
    auto sv = write("html.sv", kClean);
    auto js = (scratch()/"html.json").string(), a = (scratch()/"a.html").string(), b = (scratch()/"b.html").string();
    auto args = "--top top --root " + scratch().string() + " --json " + js + " --html ";
    run(args + a + " " + sv); run(args + b + " " + sv);
    auto j = sverity::json::parse(slurp(js)); auto html = slurp(a);
    CHECK(j.ok); CHECK(!html.empty()); CHECK_EQ(html, slurp(b));
    for (const auto& d : j.value["diagnostics"].asArray()) CHECK(html.find(d["fingerprint"].asString()) != std::string::npos);
    CHECK(html.find("INCOMPLETE") != std::string::npos);
    CHECK(html.find(scratch().string()) == std::string::npos);
}
TEST(cli_html_escapes_untrusted_paths) {
    auto sv = write("<script>&.sv", kClean), out = (scratch()/"escape.html").string();
    run("--top top --root '" + scratch().string() + "' --html '" + out + "' '" + sv + "'");
    auto html = slurp(out);
    CHECK(html.find("&lt;script&gt;&amp;.sv") != std::string::npos);
    CHECK(html.find("<script>") == std::string::npos);
}
TEST(cli_html_write_failure_sets_execution_error) {
    auto sv = write("html-write.sv", kClean);
    CHECK((run("--top top --html " + (scratch()/"missing-dir/out.html").string() + " " + sv) & 8) != 0);
    CHECK_EQ(run("--html - " + sv), 64);
}

TEST(cli_source_waivers_include_and_library_buffers) {
    auto leaf = write("waiver_leaf.sv", "module waiver_leaf(input logic clka, clkb, d, output logic y); logic src; always_ff @(posedge clka) src <= d;\n"
        "always_ff @(posedge clkb) y <= src; // sverity-waive CDC-003 owner=\"team\" reason=\"reviewed protocol\"\nendmodule // sverity-waive CDC-028 owner=\"team\" reason=\"no finding here\"\n");
    auto top = write("waiver_top.sv", "module top(input logic clka, clkb, d, output logic y); waiver_leaf u(clka,clkb,d,y); endmodule\n");
    auto included = write("waiver_include.sv", "`include \"waiver_leaf.sv\"\n" + slurp(top));
    auto out = (scratch()/"loaded-waivers.json").string();
    for (const auto& args : {"-I"+scratch().string()+" "+included, top+" -v "+leaf, top+" -y "+scratch().string()}) {
        run("--top top --sdc "+std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/_common/async.sdc --json "+out+" "+args);
        auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
        bool found = false;
        for (const auto& d : j.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-003") {
            found = true; CHECK_EQ(d["disposition"].asString(), std::string("WAIVED"));
            CHECK_EQ(d["waiver"]["source_location"]["line"].asInt(), 2);
        }
        CHECK(found);
        bool stale = false;
        for (const auto& d : j.value["diagnostics"].asArray()) stale |= d["rule_id"].asString() == "CST-022";
        CHECK(stale); // loaded includes and library units must disclose unapplied comments
    }
}
TEST(cli_waiver_template_round_trip_and_filled_fields) {
    auto source = std::string(SVERITY_SOURCE_DIR)+"/tests/fixtures/CST-006/source_valid.sv";
    auto out = (scratch()/"template-report.json").string(), tmpl = (scratch()/"template.json").string();
    run("--no-source-waivers --top top --waiver-template "+tmpl+" --json "+out+" "+source);
    auto original = sverity::json::parse(slurp(out)); CHECK(original.ok);
    auto entries = sverity::json::parse(slurp(tmpl)); CHECK(entries.ok);
    auto n = entries.value["waivers"].asArray().size(); CHECK(n > 0);
    run("--no-source-waivers --top top --waivers "+tmpl+" --json "+out+" "+source);
    auto rejected = sverity::json::parse(slurp(out)); CHECK(rejected.ok);
    size_t warnings = 0;
    for (const auto& d : rejected.value["diagnostics"].asArray()) {
        CHECK(d["disposition"].asString() != "WAIVED");
        if (d["rule_id"].asString() == "CST-006") ++warnings;
    }
    CHECK_EQ(warnings, n);
    auto filled = slurp(tmpl);
    for (size_t pos; (pos = filled.find("<REQUIRED>")) != std::string::npos;) filled.replace(pos, 10, "reviewed");
    auto edited = write("template-filled.json", filled);
    run("--no-source-waivers --top top --waivers "+edited+" --json "+out+" "+source);
    auto accepted = sverity::json::parse(slurp(out)); CHECK(accepted.ok);
    size_t waived = 0;
    for (const auto& d : accepted.value["diagnostics"].asArray()) if (d["disposition"].asString() == "WAIVED") ++waived;
    CHECK_EQ(waived, n);
    CHECK_EQ(accepted.value["summary"]["analysis_coverage"].asString(), original.value["summary"]["analysis_coverage"].asString());
}

TEST(cli_text_priority_severity_contract) {
    auto source = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/LNT-025/boundary_text_priority.sv";
    auto out = (scratch() / "text-priority.json").string();
    auto config = write("priority-policy.json", R"({"options":{"LNT-025.priority_chains":"warning"}})");
    for (const auto& setting : {std::string(""), std::string("--config " + config), std::string("--severity LNT-025=WARNING"),
                                std::string("--option LNT-025.priority_chains=warning")}) {
        run("--top top --json " + out + " " + setting + " " + source);
        auto result = sverity::json::parse(slurp(out)); CHECK(result.ok);
        size_t count = 0;
        for (const auto& d : result.value["diagnostics"].asArray()) {
            if (d["rule_id"].asString() != "LNT-025") continue;
            ++count;
            CHECK_EQ(d["severity"].asString(), std::string(setting.empty() ? "INFO" : "WARNING"));
            CHECK_EQ(d["confidence"].asString(), std::string("heuristic"));
            CHECK_EQ(d["proof_state"].asString(), std::string("NOT_APPLICABLE"));
            CHECK(d["message"].asString().find("assumed to be intended priority") != std::string::npos);
            CHECK(d["message"].asString().find("exclusivity is not proved") != std::string::npos);
        }
        CHECK_EQ(count, size_t(3));
    }
    CHECK_EQ(run("--option LNT-025.priority_chains=quiet " + source), 64);
}

TEST(cli_cdc003_advice_uses_relation_and_waiver_state) {
    const auto root = std::string(SVERITY_SOURCE_DIR);
    const auto dest = (scratch() / "advice.json").string();
    for (int state = 0; state < 3; ++state) {
        std::string args = "--goal cdc --top top --root " + root + " --json " + dest;
        if (state) args += " --sdc " + root + "/docs/tutorial/clocks.sdc";
        if (state == 2) args += " --waivers " + root + "/docs/tutorial/waivers.json";
        run(args + " " + root + "/docs/tutorial/bad.sv");
        auto parsed = sverity::json::parse(slurp(dest)); CHECK(parsed.ok); bool seen = false;
        for (const auto& d : parsed.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-003") {
            seen = true; const auto text = d["next_step"].asString();
            CHECK(text.find(state == 0 ? "Declare the clock intent" : state == 1 ? "a clock declaration does not repair" : "Retain and review the waiver justification") != std::string::npos);
        }
        CHECK(seen);
    }
}

TEST(cli_deck_claims_only_its_rules_and_keeps_every_finding) {
    const auto root = std::string(SVERITY_SOURCE_DIR);
    const auto all = (scratch() / "all.json").string(), core = (scratch() / "core.json").string();
    const auto common = " --top top --sdc " + root + "/docs/tutorial/clocks.sdc --root " + root + " " + root + "/docs/tutorial/bad.sv";
    run("--json " + all + common);
    run("--deck core --json " + core + common);
    auto full = sverity::json::parse(slurp(all)), deck = sverity::json::parse(slurp(core));
    CHECK(full.ok && deck.ok);
    CHECK_EQ(full.value["diagnostics"].asArray().size(), deck.value["diagnostics"].asArray().size() );   // nothing is hidden
    CHECK(deck.value["inputs"]["deck"].asString() == "core");
    size_t preview = 0;
    for (const auto& rule : deck.value["coverage"]["rules"].asArray()) preview += rule["preview"].asBool();
    CHECK(preview > 0);
    CHECK(deck.value["summary"]["analysis_coverage"].asString() == "INCOMPLETE");   // core rules are still partial
    CHECK_EQ(run("--deck nonsense" + common), 64);
}
