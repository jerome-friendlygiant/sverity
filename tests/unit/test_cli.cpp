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
const char* kClocked = "module top(input logic clk, d, output logic q); always_ff @(posedge clk) q <= d; endmodule\n";
const char* kClean = "module top(input logic a, output logic y); assign y = ~a; endmodule\n";
} // namespace

TEST(cli_usage_errors_exit_64) {
    CHECK_EQ(run(""), 64);                                    // no sources
    CHECK_EQ(run("--exit-policy bogus x.sv"), 64);
    CHECK_EQ(run("--date 2026-02-30 x.sv"), 64);
    CHECK_EQ(run("--max-console 5 x.sv"), 64);                // truncation needs a complete report on disk
    CHECK_EQ(run("--json - --sarif - x.sv"), 64);
}
TEST(cli_exit_code_is_a_bit_mask_and_strict_keeps_rules_that_are_missing) {
    auto sv = write("clean.sv", kClean);
    CHECK_EQ(run("--top top --goal lint " + sv), 0);           // gate: a rule partial in the tool version is a coverage statement
    int strict = run("--top top --goal lint --exit-policy strict " + sv);
    CHECK((strict & 4) != 0);          // strict: analysis INCOMPLETE, catalog rules are unimplemented/partial
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

TEST(cli_inferred_latch_reports_assigned_signal_as_metadata) {
    auto sv = write("inferred_latch_metadata.sv",
                    "module top(input logic sel, input logic a, input logic b, output logic overflow_d, output logic unrelated);\n"
                    "always_comb begin\n"
                    "  if (sel) overflow_d = a;\n"
                    "  unrelated = b;\n"
                    "end\n"
                    "endmodule\n");
    auto out = scratch() / "inferred_latch_metadata.json";
    run("--top top --goal lint --json " + out.string() + " " + sv);
    auto report = sverity::json::parse(slurp(out.string()));
    CHECK(report.ok);
    bool matched = false;
    for (const auto& diagnostic : report.value["diagnostics"].asArray()) {
        if (diagnostic["rule_id"].asString() != "LNT-013") continue;
        const auto& signals = diagnostic["signals"].asArray();
        CHECK_EQ(signals.size(), size_t(1));
        CHECK_EQ(signals[0].asString(), std::string("overflow_d"));
        CHECK(diagnostic["hierarchy"].asString() == "top");
        matched = true;
    }
    CHECK(matched);
}
TEST(cli_latch_metadata_does_not_identify_a_shadowed_module_variable) {
    auto sv = write("latch_shadow_metadata.sv",
                    "module top(input logic en,d,output logic q);\n"
                    "logic v; assign v=d;\n"
                    "always_comb begin : named\n"
                    "logic v; if(en) v=d; q=v;\n"
                    "end endmodule\n");
    auto out = scratch() / "latch_shadow_metadata.json";
    run("--top top --goal lint --json " + out.string() + " " + sv);
    auto report = sverity::json::parse(slurp(out.string()));
    CHECK(report.ok);
    bool matched = false;
    for (const auto& diagnostic : report.value["diagnostics"].asArray()) {
        if (diagnostic["rule_id"].asString() != "LNT-013") continue;
        CHECK(diagnostic["signals"].asArray().empty());
        matched = true;
    }
    CHECK(matched);
}

TEST(cli_multiple_always_assigns_reports_exact_signal_with_malformed_source) {
    auto sv = write("multiple_always_signal_metadata.sv",
                    "module top(input logic clk, input logic a, input logic b, output logic q, output logic r);\n"
                    "always_ff @(posedge clk) begin q <= a; r <= a; end\n"
                    "always_ff @(posedge clk) q <= b;\n"
                    "endmodule\n"
                    "module malformed(input logic a, output logic y) assign y = a; endmodule\n");
    auto out = scratch() / "multiple_always_signal_metadata.json";
    run("--top top --goal lint --json " + out.string() + " " + sv);
    auto report = sverity::json::parse(slurp(out.string()));
    CHECK(report.ok);
    CHECK_EQ(report.value["summary"]["execution_status"].asString(), std::string("ERROR"));
    bool multipleAlwaysAssigns = false;
    bool malformedSourceError = false;
    for (const auto& diagnostic : report.value["diagnostics"].asArray()) {
        if (diagnostic["rule_id"].asString() == "LNT-009" &&
            diagnostic["upstream_code"].asString() == "multiple-always-assigns") {
            const auto& signals = diagnostic["signals"].asArray();
            CHECK_EQ(signals.size(), size_t(1));
            CHECK_EQ(signals[0].asString(), std::string("q"));
            CHECK_EQ(diagnostic["hierarchy"].asString(), std::string("top.q"));
            CHECK_EQ(diagnostic["location"]["line"].asInt(), int64_t(3));
            multipleAlwaysAssigns = true;
        }
        if (diagnostic["origin"].asString() == "slang" &&
            diagnostic["severity"].asString() == "ERROR" &&
            diagnostic["rule_id"].asString() != "LNT-009")
            malformedSourceError = true;
    }
    CHECK(multipleAlwaysAssigns);
    CHECK(malformedSourceError);
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
    check("CDC-009/bad_named_scalar_lanes.sv", "CDC-009", "synchroniser captures");
    check("CDC-013/bad_comparable_periods.sv", "CDC-013", "comparable to or shorter");
}

TEST(cli_exclusive_input_hazards_state_the_trusted_clock_intent) {
    const std::string fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    const auto out = (scratch() / "exclusive-input-hazards.json").string();
    const std::pair<const char*, const char*> cases[] = {
        {"CDC-006", "CDC-006/bad_async_input_link_logic.sv"},
        {"CDC-007", "CDC-007/boundary_exclusive_input_tap.sv"},
        {"CDC-008", "CDC-008/boundary_exclusive_input_logic.sv"},
    };
    for (const auto& [rule, file] : cases) {
        run("--top top --sdc " + fixtures + "CDC-047/primary_input_exclusive.sdc --json " + out + " " + fixtures + file);
        const auto report = sverity::json::parse(slurp(out));
        CHECK(report.ok);
        bool trusted = false;
        for (const auto& finding : report.value["diagnostics"].asArray()) {
            if (finding["rule_id"].asString() != rule || finding["severity"].asString() != "INFO") continue;
            for (const auto& evidence : finding["evidence"].asArray())
                trusted |= evidence["kind"].asString() == "trust_model";
        }
        CHECK(trusted);
    }
}

TEST(cli_cdc_reports_register_driven_divider_as_generated_clock) {
    const std::string fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    const auto out = (scratch() / "cdc-divider-root.json").string();
    run("--top top --sdc " + fixtures + "_common/async.sdc --json " + out +
        " " + fixtures + "CDC-002/bad_free_running_divider.sv");
    const auto report = sverity::json::parse(slurp(out));
    CHECK(report.ok);
    bool generated = false;
    for (const auto& diagnostic : report.value["diagnostics"].asArray()) {
        if (diagnostic["rule_id"].asString() != "CDC-002") continue;
        generated |= diagnostic["message"].asString().find("logic-generated") != std::string::npos &&
                     diagnostic["confidence"].asString() == "unresolved";
    }
    CHECK(generated);
}

TEST(cli_cdc_clock_mux_reports_located_site_evidence) {
    const std::string fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    const auto out = (scratch() / "cdc-clock-mux-site.json").string();
    run("--top top --root " + fixtures + "CDC-019 --sdc " + fixtures +
        "_common/async.sdc --json " + out + " " + fixtures + "CDC-019/bad_clock_mux.sv");
    const auto report = sverity::json::parse(slurp(out));
    CHECK(report.ok);
    for (const auto* rule : {"CDC-019", "CDC-031"}) {
        bool located = false;
        for (const auto& d : report.value["diagnostics"].asArray()) {
            if (d["rule_id"].asString() != rule) continue;
            CHECK(d["signals"].asArray().empty());
            for (const auto& item : d["evidence"].asArray()) {
                if (item["kind"].asString() == "site_signal" &&
                    item["text"].asString() == "top.mclk" &&
                    item["location"]["file"].asString() == "bad_clock_mux.sv" &&
                    item["location"]["line"].asInt() == 8)
                    located = true;
            }
        }
        CHECK(located);
    }
}

TEST(cli_cdc_output_cones_disclose_reconvergence_word_and_delivery_contracts) {
    const std::string fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    const auto out = (scratch() / "cdc-output-contracts.json").string();
    auto runFixture = [&](const std::string& sdc, const std::string& design) {
        run("--top top --sdc " + fixtures + sdc + " --json " + out + " " + fixtures + design);
        auto result = sverity::json::parse(slurp(out));
        CHECK(result.ok);
        return result.value;
    };
    auto contains = [](const sverity::json::Value& diagnostic, const std::string& type, const std::string& value) {
        for (const auto& evidence : diagnostic["evidence"].asArray())
            if (evidence["kind"].asString() == type && evidence["text"].asString().find(value) != std::string::npos) return true;
        return false;
    };
    auto packed = runFixture("CDC-010/output_reconvergence.sdc", "CDC-010/output_reconvergence.sv");
    bool reconvergesAtXorBit = false, coherentPackedWord = false;
    bool repeatedCopyWarning = false, constantPaddingWarning = false, independentPortWarning = false, opaqueSourceWarning = false;
    for (const auto& diagnostic : packed["diagnostics"].asArray()) {
        const auto rule = diagnostic["rule_id"].asString();
        const auto message = diagnostic["message"].asString();
        if (rule == "CDC-010" && contains(diagnostic, "output_bit", "xor_out") &&
            diagnostic["proof_state"].asString() == "NOT_ATTEMPTED" && diagnostic["confidence"].asString() == "unresolved")
            reconvergesAtXorBit = true;
        if (rule == "CST-012" && message.find("packed_word") != std::string::npos &&
            contains(diagnostic, "output_word_bit", "packed_word[0]") && contains(diagnostic, "output_word_bit", "packed_word[1]") &&
            diagnostic["proof_state"].asString() == "NOT_ATTEMPTED" && diagnostic["confidence"].asString() == "unresolved") coherentPackedWord = true;
        if (rule == "CST-012" && message.find("packs independently synchronized source bits") != std::string::npos &&
            message.find("repeated") != std::string::npos) repeatedCopyWarning = true;
        if (rule == "CST-012" && message.find("packs independently synchronized source bits") != std::string::npos &&
            message.find("padded") != std::string::npos) constantPaddingWarning = true;
        if (rule == "CST-012" && message.find("packs independently synchronized source bits") != std::string::npos &&
            (message.find("out_a") != std::string::npos || message.find("out_b") != std::string::npos))
            independentPortWarning = true;
        if (rule == "CST-012" && message.find("packs independently synchronized source bits") != std::string::npos &&
            message.find("opaque_mix") != std::string::npos) opaqueSourceWarning = true;
    }
    CHECK(reconvergesAtXorBit);
    CHECK(coherentPackedWord);
    CHECK(!repeatedCopyWarning);
    CHECK(!constantPaddingWarning);
    CHECK(!independentPortWarning);
    CHECK(!opaqueSourceWarning);

    auto vector = runFixture("CDC-010/output_vector_origins.sdc", "CDC-010/output_vector_origins.sv");
    unsigned reconvergenceCount = 0, coherenceCount = 0;
    for (const auto& diagnostic : vector["diagnostics"].asArray()) {
        if (diagnostic["rule_id"].asString() == "CDC-010" && contains(diagnostic, "output_bit", "observed[2]")) {
            ++reconvergenceCount;
            CHECK(contains(diagnostic, "synchronized_source_bit", "launched[0]"));
            CHECK(contains(diagnostic, "synchronized_source_bit", "launched[1]"));
        }
        if (diagnostic["rule_id"].asString() == "CST-012" && contains(diagnostic, "output_word_bit", "observed[0]")) {
            ++coherenceCount;
            CHECK(contains(diagnostic, "output_word_bit", "observed[1]"));
            CHECK(!contains(diagnostic, "output_word_bit", "observed[2]"));
        }
    }
    CHECK(reconvergenceCount == 1);
    CHECK(coherenceCount == 1);

    auto event = runFixture("CDC-013/synchronized_input_to_output.sdc", "CDC-013/synchronized_input_to_output.sv");
    bool deliveryOpen = false;
    for (const auto& diagnostic : event["diagnostics"].asArray())
        if (diagnostic["rule_id"].asString() == "CDC-013" &&
            diagnostic["message"].asString().find("minimum duration") != std::string::npos &&
            contains(diagnostic, "output_bit", "event_out") &&
            diagnostic["proof_state"].asString() == "NOT_ATTEMPTED" && diagnostic["confidence"].asString() == "unresolved") deliveryOpen = true;
    CHECK(deliveryOpen);
}

TEST(cli_cdc007_taps_are_evidence_not_waiver_handles) {
    const auto sdc = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc";
    const auto sv = write("cdc007-tap.sv",
        "module top(input logic clka, clkb, d, output logic y);\n"
        "  logic s1, s2;\n"
        "  always_ff @(posedge clkb) begin s1 <= d; s2 <= s1; end\n"
        "  assign y = ~s1;\n"
        "endmodule\n");
    const auto waivers = write("cdc007-tap-waiver.json",
        R"({"waivers":[{"rule":"CDC-007","signal":"top.y","owner":"review","reason":"tap scope regression"}]})");
    const auto firstOut = (scratch() / "cdc007-tap-first.json").string();
    const auto renamedOut = (scratch() / "cdc007-tap-renamed.json").string();
    auto readTap = [&](const std::string& out) {
        auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
        for (const auto& d : j.value["diagnostics"].asArray())
            if (d["rule_id"].asString() == "CDC-007") return d;
        CHECK(false);
        return sverity::json::Value{};
    };
    auto args = "--top top --goal cdc --root " + scratch().string() + " --sdc " + sdc + " --waivers " + waivers + " --json ";
    run(args + firstOut + " " + sv);
    auto first = readTap(firstOut);
    CHECK_EQ(first["signals"].asArray().size(), size_t{1});
    CHECK_EQ(first["signals"].asArray()[0].asString(), std::string("top.d"));
    CHECK(first["disposition"].asString() != "WAIVED");
    bool locatedTap = false;
    for (const auto& e : first["evidence"].asArray())
        if (e["kind"].asString() == "downstream_tap" && e["text"].asString() == "top.y")
            locatedTap = e["location"]["line"].asInt() > 0 && e["location"]["column"].asInt() > 0;
    CHECK(locatedTap);

    write("cdc007-tap.sv",
        "module top(input logic clka, clkb, d, output logic z);\n"
        "  logic s1, s2;\n"
        "  always_ff @(posedge clkb) begin s1 <= d; s2 <= s1; end\n"
        "  assign z = ~s1;\n"
        "endmodule\n");
    run(args + renamedOut + " " + sv);
    auto renamed = readTap(renamedOut);
    CHECK_EQ(first["fingerprint"].asString(), renamed["fingerprint"].asString());
    CHECK(renamed["disposition"].asString() != "WAIVED");
    bool renamedTap = false;
    for (const auto& e : renamed["evidence"].asArray())
        if (e["kind"].asString() == "downstream_tap" && e["text"].asString() == "top.z")
            renamedTap = e["location"]["line"].asInt() > 0 && e["location"]["column"].asInt() > 0;
    CHECK(renamedTap);
}

TEST(cli_cdc_input_delay_evidence_shows_effective_modes_and_mixed_intent) {
    const auto fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/";
    const auto out = (scratch() / "cdc-input-delay-evidence.json").string();
    run("--top top --json " + out + " --sdc " + fixtures + "CDC-029/registered_bit_modes.sdc " +
        fixtures + "CDC-029/bad_registered_input_bit_modes.sv");
    auto registered = sverity::json::parse(slurp(out)); CHECK(registered.ok);
    bool asyncMin = false, sameMax = false;
    for (const auto& d : registered.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-029")
        for (const auto& e : d["evidence"].asArray()) if (e["kind"].asString() == "input_delay_contract") {
            asyncMin |= e["text"].asString().find("effective min mode") != std::string::npos &&
                        e["text"].asString().find("src_clk") != std::string::npos;
            sameMax |= e["text"].asString().find("effective max mode") != std::string::npos &&
                       e["text"].asString().find("dst_clk") != std::string::npos;
        }
    CHECK(asyncMin); CHECK(sameMax);

    run("--top top --json " + out + " --sdc " + fixtures + "CDC-029/output_mixed_unknown.sdc " +
        fixtures + "CDC-029/bad_output_mixed_unknown.sv");
    auto mixed = sverity::json::parse(slurp(out)); CHECK(mixed.ok);
    bool disclosed = false, additive = false;
    for (const auto& d : mixed.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-029")
        for (const auto& e : d["evidence"].asArray()) {
            disclosed |= e["kind"].asString() == "mixed_clock_intent";
            additive |= e["kind"].asString() == "input_delay_contract" &&
                        e["text"].asString().find("additive") != std::string::npos;
        }
    CHECK(disclosed); CHECK(additive);
}

TEST(cli_cdc013_explicit_boundary_contract_validates_and_reports_sampling_evidence) {
    const auto fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-013/";
    const auto out = (scratch() / "cdc013-boundary.json").string();
    const auto common = "--top top --goal cdc --root " + scratch().string() + " --sdc " + fixtures + "boundary_input_sync.sdc --json ";
    auto hasEvidence = [](const sverity::json::Value& diagnostic, const std::string& kind, const std::string& text) {
        for (const auto& evidence : diagnostic["evidence"].asArray())
            if (evidence["kind"].asString() == kind && evidence["text"].asString().find(text) != std::string::npos) return true;
        return false;
    };
    run(common + out + " --config " + fixtures + "boundary_short.json " + fixtures + "boundary_input_sync.sv");
    auto shortRun = sverity::json::parse(slurp(out)); CHECK(shortRun.ok);
    bool loss = false, contractEvidence = false, internalSampling = false;
    for (const auto& d : shortRun.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-013") {
        loss |= d["message"].asString().find("can be lost at arbitrary phase") != std::string::npos && d["confidence"].asString() == "established";
        for (const auto& e : d["evidence"].asArray()) {
            contractEvidence |= e["kind"].asString() == "boundary_event_contract" && e["text"].asString().find("launch_clock=launch") != std::string::npos;
            internalSampling |= e["kind"].asString() == "sampling_intervals" && e["text"].asString().find("destination period 20") != std::string::npos;
        }
    }
    CHECK(loss); CHECK(contractEvidence); CHECK(internalSampling);

    run(common + out + " --config " + fixtures + "boundary_equal.json " + fixtures + "boundary_input_sync.sv");
    auto equalRun = sverity::json::parse(slurp(out)); CHECK(equalRun.ok);
    bool equality = false;
    for (const auto& d : equalRun.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-013")
        equality |= d["severity"].asString() == "WARNING" && d["confidence"].asString() == "unresolved" &&
            d["message"].asString().find("1.5-period review margin") != std::string::npos;
    CHECK(equality);

    run(common + out + " --config " + fixtures + "boundary_unknown.json " + fixtures + "boundary_input_sync.sv");
    auto unknownRun = sverity::json::parse(slurp(out)); CHECK(unknownRun.ok);
    bool unresolved = false;
    for (const auto& d : unknownRun.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-013")
        unresolved |= d["confidence"].asString() == "unresolved" && hasEvidence(d, "boundary_contract_validation", "launch clock is not declared");
    CHECK(unresolved);

    const auto invalid = write("cdc013-invalid-contract.json", R"({"boundary_events":[{"port":"event_i*","launch_clock":"launch","protocol":"event","duration":{"value":4,"unit":"ns"},"delivery":"required"}]})");
    CHECK_EQ(run("--config " + invalid + " " + fixtures + "boundary_input_sync.sv"), 64);

    const auto mixed = write("cdc013-mixed.sdc",
        "create_clock -name launch -period 4 [get_ports launch_clk]\n"
        "create_clock -name sample -period 20 [get_ports sample_clk]\n"
        "create_clock -name other -period 7 [get_ports other_clk]\n"
        "set_clock_groups -asynchronous -group launch -group sample\n"
        "set_clock_groups -asynchronous -group other -group sample\n"
        "set_input_delay -clock launch -min 1 [get_ports event_i]\n"
        "set_input_delay -clock other -max 1 [get_ports event_i]\n");
    run("--top top --goal cdc --root " + scratch().string() + " --sdc " + mixed + " --config " + fixtures + "boundary_short.json --json " + out + " " + fixtures + "boundary_input_sync.sv");
    auto mixedRun = sverity::json::parse(slurp(out)); CHECK(mixedRun.ok);
    bool mixedUnresolved = false;
    for (const auto& d : mixedRun.value["diagnostics"].asArray()) if (d["rule_id"].asString() == "CDC-013")
        mixedUnresolved |= d["confidence"].asString() == "unresolved" && hasEvidence(d, "boundary_contract_validation", "conflicts with effective set_input_delay");
    CHECK(mixedUnresolved);
}

TEST(cli_cdc013_exact_boundary_event_sampling_premises) {
    const auto fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-013/";
    const auto out = (scratch() / "cdc013-boundary-recheck.json").string();
    const auto clocks = slurp(fixtures + "boundary_input_sync.sdc");
    auto contract = [&](const std::string& port, const std::string& width, const std::string& units) {
        return write("cdc013-recheck-contract.json",
            R"({"boundary_events":[{"port":")" + port +
            R"(","launch_clock":"launch","protocol":"event","delivery":"required","duration":{"value":)" +
            width + R"(,"unit":")" + units + R"("}}]})");
    };
    auto check = [&](const std::string& design, const std::string& sdc, const std::string& config,
                     const std::string& confidence, const std::string& message) {
        const auto constraints = write("cdc013-recheck-sdc.sdc", sdc);
        const auto exit = run("--top top --goal cdc --root " + scratch().string() + " --sdc " + constraints +
                              " --config " + config + " --json " + out + " " + fixtures + design);
        CHECK(exit >= 0 && exit < 8);
        const auto report = sverity::json::parse(slurp(out));
        CHECK(report.ok);
        unsigned findings = 0;
        for (const auto& d : report.value["diagnostics"].asArray()) {
            if (d["rule_id"].asString() != "CDC-013") continue;
            ++findings;
            CHECK_EQ(d["confidence"].asString(), confidence);
            CHECK(d["message"].asString().find(message) != std::string::npos);
        }
        CHECK_EQ(findings, 1u);
    };
    check("boundary_ascending_input.sv", clocks, contract("event_i[4]", "4", "ns"), "established", "event_i[4]");
    check("boundary_ascending_input.sv", clocks, contract("event_i[0]", "4", "ns"), "unresolved", "one exact");
    check("boundary_masked_input.sv", clocks, contract("event_i", "4", "ns"), "unresolved", "unconditional");
    check("boundary_input_sync.sv", clocks, contract("event_i", "1e308", "s"), "unresolved", "finite positive");
    check("boundary_input_sync.sv", clocks, contract("event_i", "21", "ns"), "unresolved", "1.5-period review margin");
    check("boundary_input_sync.sv", clocks, contract("event_i", "30", "ns"), "heuristic", "at least one");
    check("boundary_input_sync.sv", clocks + "create_clock -name launch -period 40 [get_ports launch_clk]\n",
          contract("event_i", "1", "source_cycles"), "heuristic", "at least one");
    check("boundary_input_sync.sv", clocks + "set_output_delay -clock sample 1 [get_ports event_o]\n",
          contract("event_i", "4", "ns"), "established", "can be lost");
    check("boundary_input_sync.sv", clocks + "set_clock_groups -logically_exclusive -group launch -group sample\n",
          contract("event_i", "4", "ns"), "unresolved", "not established asynchronous");
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
TEST(cli_dynamic_index_and_divisor_options) {
    auto sv = write("dynamic-options.sv", "module top(input logic [2:0] i, input logic [7:0] d, output logic bit_out, output logic [7:0] q);\n"
        "  logic [5:0] data; assign bit_out = data[i]; assign q = 8'd42 / d;\nendmodule\n");
    auto config = write("dynamic-options.json", R"({"options":{"LNT-006.dynamic_index":true,"LNT-037.dynamic_divisor":true}})");
    auto out = (scratch() / "dynamic-options-report.json").string();
    for (const auto& setting : {std::string(""), std::string("--config ") + config,
                                 std::string("--option LNT-006.dynamic_index=true --option LNT-037.dynamic_divisor=true")}) {
        fs::remove(out);
        CHECK(run("--top top --json " + out + " " + setting + " " + sv) != 64);
        auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
        bool index = false, divisor = false;
        for (const auto& d : j.value["diagnostics"].asArray()) {
            index |= d["rule_id"].asString() == "LNT-006";
            divisor |= d["rule_id"].asString() == "LNT-037";
        }
        CHECK(index);   // a free index is an obligation by default
        CHECK(divisor);   // a free divisor is an obligation by default too
    }
    {   // the default is switched off by the option
        fs::remove(out);
        CHECK(run("--top top --json " + out + " --option LNT-006.dynamic_index=false " + sv) != 64);
        auto j = sverity::json::parse(slurp(out)); CHECK(j.ok);
        for (const auto& d : j.value["diagnostics"].asArray()) CHECK(d["rule_id"].asString() != "LNT-006");
    }
    for (const char* key : {"LNT-006.dynamic_index", "LNT-037.dynamic_divisor"}) {
        for (const char* value : {"1", "\"true\"", "null"}) {
            auto invalid = write("dynamic-invalid.json", std::string("{\"options\":{\"") + key + "\":" + value + "}}");
            CHECK_EQ(run("--config " + invalid + " " + sv), 64);
        }
        CHECK_EQ(run(std::string("--option ") + key + "=yes " + sv), 64);
    }
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
    auto out=cfgRun(R"({"severity":{"CST-011":"INFO"}})", "", kClocked); auto j=sverity::json::parse(slurp(out)); bool found=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CST-011") found=d["severity"].asString()=="WARNING" && d["requested_severity"].asString()=="INFO" && !d["severity_override_reason"].asString().empty();
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
    auto out=cfgRun(R"({"severity":{"CST-011":"ERROR"}})", "--severity CST-011=INFO", kClocked);
    auto j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& d:j.value["diagnostics"].asArray()) if(d["rule_id"].asString()=="CST-011") seen=d["requested_severity"].asString()=="INFO" && d["severity"].asString()=="WARNING";
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
    auto out=cfgRun(R"({"enable":["CST-020"],"disable":["CST-020"]})", "--enable-rule CST-020");
    auto j=sverity::json::parse(slurp(out)); bool seen=false;
    for(const auto& r:j.value["coverage"]["rules"].asArray()) if(r["rule_id"].asString()=="CST-020") { seen=true; CHECK_EQ(r["state"].asString(),std::string("UNIMPLEMENTED")); }
    CHECK(seen); CHECK((j.value["summary"]["exit_code_strict"].asInt() & 4) != 0);   // the rule stays unimplemented: strict still reports it
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
        if (rule["rule_id"].asString() == "CDC-012") found = rule["state"].asString() == "PARTIAL" && !rule["reason"].asString().empty();
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
    CHECK(r["summary"]["tool_coverage"]["rules_disabled"].asInt() > 0); CHECK((r["summary"]["exit_code_strict"].asInt() & 4) != 0);   // disabled by the goal: stated, and INCOMPLETE under strict
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
TEST(cli_goal_cdc_retains_slang_advisory_and_disabled_disclosure) {
    auto source="module top(input logic a, output logic y); logic unused_signal; assign y=a; endmodule\n";
    auto r=goalReport("slang-warning",source,"--goal cdc"); bool warning=false, disclosed=false;
    for(const auto& d:r["diagnostics"].asArray()) if(d["rule_id"].asString()=="LNT-011" && d["origin"].asString()=="slang" && d["severity"].asString()=="INFO" && d["upstream_severity"].asString()=="warning") warning=true;
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
        // every clock is constrained (the data input d has no input delay, which CST-011 reports on its own)
        CHECK(d["rule_id"].asString() != "CST-011" || d["message"].asString().find("no clock constraint") == std::string::npos);
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
        if (msg.find("domain 'top.other'") != std::string::npos) { foreign = true; CHECK_EQ(d["severity"].asString(), std::string("ERROR")); }
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
        for (const char* key : {"OPEN", "REVIEW_REQUIRED", "RESOLVED", "WAIVED", "OBLIGATION", "NOT_ANALYSED"}) row += " " + std::to_string(rule["disposition_counts"][key].asInt());
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

TEST(cli_deck_cdc_enables_coupled_rdc_without_rdc_goal) {
    const auto root = std::string(SVERITY_SOURCE_DIR);
    const auto out = (scratch() / "cdc_deck.json").string();
    const auto common = " --profile asic --goal cdc --deck cdc --top top --sdc " + root + "/docs/tutorial/clocks.sdc --root " + root + " " + root + "/docs/tutorial/bad.sv";
    run("--json " + out + common);
    auto deck = sverity::json::parse(slurp(out));
    CHECK(deck.ok);
    CHECK_EQ(deck.value["inputs"]["deck"].asString(), std::string("cdc"));
    size_t inDeckCount = 0, previewCount = 0;
    bool rdc003Active = false, rdc004Disabled = false, cdc012Preview = false;
    for (const auto& rule : deck.value["coverage"]["rules"].asArray()) {
        const auto id = rule["rule_id"].asString();
        const bool preview = rule["preview"].asBool();
        if (preview) previewCount++; else inDeckCount++;
        if (id == "RDC-003") rdc003Active = !preview && rule["state"].asString() != "DISABLED";
        if (id == "RDC-004") rdc004Disabled = preview && rule["state"].asString() == "DISABLED";
        if (id == "CDC-012") cdc012Preview = preview;
    }
    CHECK_EQ(inDeckCount, 60u);
    CHECK(previewCount > 0);
    CHECK(rdc003Active);
    CHECK(rdc004Disabled);
    CHECK(cdc012Preview);
}

TEST(cli_cdc_deck_clean_complete_vs_five_adverse_cases) {
    const auto root = std::string(SVERITY_SOURCE_DIR);
    // Clean constrained design reaches COMPLETE analysis_coverage under --profile asic --goal cdc --deck cdc.
    const auto cleanSv = write("cdc_clean.sv",
        "module top(input logic clk, d, output logic q);\n"
        "  always_ff @(posedge clk) q <= d;\n"
        "endmodule\n");
    const auto cleanSdc = write("cdc_clean.sdc",
        "create_clock -name clk -period 10 [get_ports clk]\n"
        "set_input_delay -clock clk 1.0 [get_ports d]\n");
    const auto cleanOut = (scratch() / "cdc_clean.json").string();
    run("--profile asic --goal cdc --deck cdc --top top --sdc " + cleanSdc + " --json " + cleanOut + " " + cleanSv);
    auto clean = sverity::json::parse(slurp(cleanOut));
    CHECK(clean.ok);
    CHECK_EQ(clean.value["summary"]["analysis_coverage"].asString(), std::string("COMPLETE"));
    CHECK_EQ(clean.value["summary"]["review_status"].asString(), std::string("PASS"));
    CHECK_EQ(clean.value["summary"]["exit_code_strict"].asInt(), 0);

    // A design with an asynchronous reset and recognised reset synchronisers stays COMPLETE under the CDC deck: the RDC-011 and RDC-012
    // markers belong to those rules, which this deck does not run.
    const auto syncSv = write("cdc_reset_sync.sv",
        "module rsync(input logic clk, arst_n, output logic rst_n);\n"
        "  logic s1;\n"
        "  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; rst_n <= 1'b0; end else begin s1 <= 1'b1; rst_n <= s1; end\n"
        "endmodule\n"
        "module top(input logic clka, clkb, por_n, da, db, output logic qa, qb);\n"
        "  logic ra_n, rb_n;\n"
        "  rsync ua(.clk(clka), .arst_n(por_n), .rst_n(ra_n));\n"
        "  rsync ub(.clk(clkb), .arst_n(por_n), .rst_n(rb_n));\n"
        "  always_ff @(posedge clka or negedge ra_n) if (!ra_n) qa <= 1'b0; else qa <= da;\n"
        "  always_ff @(posedge clkb or negedge rb_n) if (!rb_n) qb <= 1'b0; else qb <= db;\n"
        "endmodule\n");
    const auto syncSdc = write("cdc_reset_sync.sdc",
        "create_clock -name clka -period 10 [get_ports clka]\n"
        "create_clock -name clkb -period 7 [get_ports clkb]\n"
        "set_clock_groups -asynchronous -group {clka} -group {clkb}\n"
        "set_input_delay -clock clka 1.0 [get_ports da]\n"
        "set_input_delay -clock clkb 1.0 [get_ports db]\n"
        "set_input_delay -clock clka 1.0 [get_ports por_n]\n");
    const auto syncOut = (scratch() / "cdc_reset_sync.json").string();
    run("--profile asic --goal cdc --deck cdc --exit-policy strict --top top --sdc " + syncSdc + " --json " + syncOut + " " + syncSv);
    auto sync = sverity::json::parse(slurp(syncOut));
    CHECK(sync.ok);
    CHECK_EQ(sync.value["summary"]["analysis_coverage"].asString(), std::string("COMPLETE"));
    CHECK_EQ(sync.value["summary"]["exit_code_strict"].asInt(), 0);

    // All 5 adverse readiness cases must force INCOMPLETE coverage.
    struct Adverse { const char* name; std::string sv; std::string sdc; };
    const std::vector<Adverse> cases = {
        {"two_async_aliases", root + "/tests/fixtures/CST-003/unknown_two_async_aliases_one_root.sv", root + "/tests/fixtures/CST-003/two_async_aliases.sdc"},
        {"gen_src_mismatch",  root + "/tests/fixtures/CST-013/bad_source_mismatch.sv",              root + "/tests/fixtures/CST-013/source_mismatch.sdc"},
        {"through_exception", root + "/tests/fixtures/CDC-038/unknown_through_exception_incomplete.sv", root + "/tests/fixtures/CDC-038/through_net.sdc"},
        {"netlist_selector",  root + "/tests/fixtures/CST-002/unknown_netlist_selector_exception.sv",   root + "/tests/fixtures/CST-002/netlist_selector.sdc"},
        {"static_subroutine", root + "/tests/fixtures/CST-010/unknown_subroutine_static_state.sv",      root + "/tests/fixtures/CDC-003/async.sdc"},
    };
    for (const auto& c : cases) {
        const auto out = (scratch() / (std::string("adverse_") + c.name + ".json")).string();
        run("--profile asic --goal cdc --deck cdc --top top --sdc " + c.sdc + " --json " + out + " " + c.sv);
        auto j = sverity::json::parse(slurp(out));
        CHECK(j.ok);
        CHECK_EQ(j.value["summary"]["analysis_coverage"].asString(), std::string("INCOMPLETE"));
        CHECK((j.value["summary"]["exit_code_strict"].asInt() & 4) != 0);
    }
}

TEST(cli_rdc_deck_clean_complete_vs_five_adverse_cases) {
    const auto root = std::string(SVERITY_SOURCE_DIR);
    // A constrained design with one raw reset released through two recognised synchronisers, a declared single always-on domain and a
    // reviewed RDC-013 waiver narrowed to the reset root exits 0 with COMPLETE coverage.
    const auto sv = write("rdc_clean.sv",
        "module rsync(input logic clk, arst_n, output logic rst_n);\n"
        "  logic s1;\n"
        "  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; rst_n <= 1'b0; end else begin s1 <= 1'b1; rst_n <= s1; end\n"
        "endmodule\n"
        "module top(input logic clka, clkb, por_n, da, db, output logic qa, qb);\n"
        "  logic ra_n, rb_n;\n"
        "  rsync ua(.clk(clka), .arst_n(por_n), .rst_n(ra_n));\n"
        "  rsync ub(.clk(clkb), .arst_n(por_n), .rst_n(rb_n));\n"
        "  always_ff @(posedge clka or negedge ra_n) if (!ra_n) qa <= 1'b0; else qa <= da;\n"
        "  always_ff @(posedge clkb or negedge rb_n) if (!rb_n) qb <= 1'b0; else qb <= db;\n"
        "endmodule\n");
    const std::string clocks =
        "create_clock -name clka -period 10 [get_ports clka]\n"
        "create_clock -name clkb -period 7 [get_ports clkb]\n"
        "set_clock_groups -asynchronous -group {clka} -group {clkb}\n"
        "set_input_delay -clock clka 1.0 [get_ports da]\n"
        "set_input_delay -clock clkb 1.0 [get_ports db]\n";
    const auto sdc = write("rdc_clean.sdc", clocks + "set_input_delay -clock clka 1.0 [get_ports por_n]\n");
    const auto sdcNoReset = write("rdc_clean_no_reset_delay.sdc", clocks);
    const auto intent = write("rdc_power_intent.json", R"({"power_intent":{"model":"single_always_on"}})");
    const auto waiver = write("rdc_waiver.json", R"({"waivers":[{"rule":"RDC-013","signal":"top.por_n","owner":"review","reason":"STA sign-off: recovery, removal and pulse width reviewed"}]})");
    const std::string base = "--profile asic --goal rdc --deck rdc --exit-policy strict --top top ";
    struct Result { int exit = 0; sverity::json::Value report; };
    const auto go = [&](const std::string& name, const std::string& args) {
        const auto out = (scratch() / (name + ".json")).string();
        Result r;
        r.exit = run(base + args + " --json " + out);
        auto parsed = sverity::json::parse(slurp(out));
        CHECK(parsed.ok);
        r.report = std::move(parsed.value);
        return r;
    };
    const auto disposition = [](const Result& r, const std::string& rule, const std::string& severity) {
        std::string found;
        for (const auto& d : r.report["diagnostics"].asArray())
            if (d["rule_id"].asString() == rule && d["severity"].asString() == severity) found = d["disposition"].asString();
        return found;
    };
    const auto reasons = [](const Result& r, const std::string& scope) {
        std::string text;
        for (const auto& s : r.report["coverage"]["scopes"].asArray())
            if (s["scope"].asString() == scope) for (const auto& why : s["reasons"].asArray()) text += why.asString() + "\n";
        return text;
    };
    const auto coverage = [](const Result& r, const std::string& scope) {
        for (const auto& s : r.report["coverage"]["scopes"].asArray())
            if (s["scope"].asString() == scope) return s["analysis_coverage"].asString();
        return std::string();
    };
    const auto cleanArgs = "--sdc " + sdc + " --config " + intent + " --waivers " + waiver + " " + sv;

    const auto clean = go("rdc_clean", cleanArgs);
    CHECK_EQ(clean.exit, 0);
    CHECK_EQ(clean.report["summary"]["analysis_coverage"].asString(), std::string("COMPLETE"));
    CHECK_EQ(clean.report["summary"]["review_status"].asString(), std::string("PASS"));
    CHECK_EQ(clean.report["summary"]["exit_code_strict"].asInt(), 0);
    CHECK_EQ(disposition(clean, "RDC-013", "WARNING"), std::string("WAIVED"));
    CHECK(!disposition(clean, "RDC-012", "INFO").empty());
    CHECK(disposition(clean, "RDC-012", "WARNING").empty());

    // (1) no power intent: RDC-012 WARNING and the reset-extraction scope names it
    const auto noIntent = go("rdc_no_intent", "--sdc " + sdc + " --waivers " + waiver + " " + sv);
    CHECK_EQ(noIntent.exit, 6);
    CHECK(!disposition(noIntent, "RDC-012", "WARNING").empty());
    CHECK(reasons(noIntent, "reset-extraction").find("RDC-012") != std::string::npos);
    CHECK_EQ(noIntent.report["summary"]["analysis_coverage"].asString(), std::string("INCOMPLETE"));

    // (2) no waiver: coverage stays COMPLETE, RDC-013 is open for review and the gate fails on it alone
    const auto noWaiver = go("rdc_no_waiver", "--sdc " + sdc + " --config " + intent + " " + sv);
    CHECK_EQ(noWaiver.exit, 2);
    CHECK_EQ(noWaiver.report["summary"]["analysis_coverage"].asString(), std::string("COMPLETE"));
    CHECK_EQ(disposition(noWaiver, "RDC-013", "WARNING"), std::string("REVIEW_REQUIRED"));

    // (3) the reset input has no input delay
    const auto rawSv = write("rdc_raw_reset.sv",
        "module top(input logic clka, por_n, da, output logic qa);\n"
        "  always_ff @(posedge clka or negedge por_n) if (!por_n) qa <= 1'b0; else qa <= da;\n"
        "endmodule\n");
    const auto unconstrained = go("rdc_unconstrained", "--sdc " + sdcNoReset + " --config " + intent + " --waivers " + waiver + " " + rawSv);
    CHECK((unconstrained.report["summary"]["exit_code_strict"].asInt() & 4) != 0);
    CHECK(!disposition(unconstrained, "RDC-002", "WARNING").empty());
    CHECK(reasons(unconstrained, "reset-extraction").find("unconstrained") != std::string::npos);

    // (4) two independent reset sources whose sequencing matters
    const auto twoSources = go("rdc_two_sources", "--sdc " + root + "/tests/fixtures/_common/async.sdc --config " + intent + " " + root + "/tests/fixtures/RDC-011/bad_two_reset_sources.sv");
    CHECK((twoSources.report["summary"]["exit_code_strict"].asInt() & 4) != 0);
    CHECK(!disposition(twoSources, "RDC-011", "WARNING").empty());
    CHECK(reasons(twoSources, "reset-extraction").find("reset sequencing") != std::string::npos);

    // (5) a declared release order the structure cannot establish
    const auto contracts = write("rdc_contracts_intent.json",
        R"({"power_intent":{"model":"single_always_on"},"reset_contracts":{"sources":[{"id":"a","root":"top.a_rst","active":"low"},{"id":"b","root":"top.b_rst","active":"low"}],)"
        R"("release_order":[{"id":"a_before_b","before":"a","after":"b","clock":"clkb","edge":"posedge","min_cycles":2}]}})");
    const auto bypass = go("rdc_release_bypass", "--sdc " + root + "/tests/fixtures/_common/async.sdc --config " + contracts + " " + root + "/tests/fixtures/CST-016/unknown_release_bypass.sv");
    CHECK((bypass.report["summary"]["exit_code_strict"].asInt() & 4) != 0);
    CHECK(!disposition(bypass, "CST-016", "WARNING").empty());
    CHECK_EQ(coverage(bypass, "reset-contracts"), std::string("INCOMPLETE"));

    // any power intent model but single_always_on is a configuration error
    const auto badIntent = write("rdc_bad_intent.json", R"({"power_intent":{"model":"upf"}})");
    CHECK_EQ(run(base + "--sdc " + sdc + " --config " + badIntent + " " + sv), 64);
}

TEST(cli_single_unit_lints_every_source_file) {
    // With --single-unit all files form one syntax tree; native lint must still see each file as user code.
    const auto first = write("unit_first.sv", "module leaf(input logic clk, input logic d, output logic q);\n"
                                              "  always_ff @(posedge clk) q <= d;\nendmodule\n");
    const auto second = write("unit_second.sv", "module top(input logic clk, input logic d, output logic q);\n"
                                                "  logic r;\n  always_ff @(posedge clk) begin r = d; q <= r; end\n"
                                                "  leaf u_leaf(.clk(clk), .d(d), .q());\nendmodule\n");
    for (const std::string mode : {"", "--single-unit "}) {
        const auto out = (scratch() / "single_unit.json").string();
        run(mode + "--top top --goal lint --json " + out + " " + first + " " + second);
        auto js = sverity::json::parse(slurp(out));
        CHECK(js.ok);
        bool blocking = false;   // LNT-015: blocking write to state in a clocked block, in the second file
        for (const auto& d : js.value["diagnostics"].asArray())
            blocking = blocking || (d["rule_id"].asString() == "LNT-015" && d["location"]["file"].asString().find("unit_second.sv") != std::string::npos);
        CHECK(blocking);
    }
}

TEST(cli_slang_style_warnings_are_advisory_and_keep_upstream_severity) {
    // A redundant default in a full unique case is slang -Wextra style: INFO, with slang's own severity recorded.
    const auto sv = write("redundant_default.sv", "module top(input logic [1:0] s, output logic y);\n  always_comb begin\n"
                                                  "    unique case (s)\n      2'd0: y = 1'b0;\n      2'd1, 2'd2, 2'd3: y = 1'b1;\n"
                                                  "      default: y = 1'b0;\n    endcase\n  end\nendmodule\n");
    const auto out = (scratch() / "redundant_default.json").string();
    run("--top top --goal lint --json " + out + " " + sv);
    auto js = sverity::json::parse(slurp(out));
    CHECK(js.ok);
    bool seen = false;
    for (const auto& d : js.value["diagnostics"].asArray())
        if (d["rule_id"].asString() == "SLANG-case-redundant-default") {
            seen = true;
            CHECK(d["severity"].asString() == "INFO");
            CHECK(d["upstream_severity"].asString() == "warning");
        }
    CHECK(seen);
}

// ---- exit policy gate: obligations and gaps are counted, not blocking, unless strict or new against a baseline
namespace {
const char* kOneFlop = "module top(input clk, rst_n, d, output logic q);\n"
    "  always_ff @(posedge clk or negedge rst_n)\n    if (!rst_n) q <= 0; else q <= d;\nendmodule\n";
// An idiomatic synchronous FIFO beside an enum FSM: only obligations and gaps, no defect.
const char* kFifoFsm = R"(module top #(parameter int W = 8, parameter int DEPTH = 16, parameter int AW = 4) (
  input logic clk, rst_n, wr_en, rd_en, input logic [W-1:0] wdata, output logic [W-1:0] rdata, output logic full, empty,
  input logic start, done, output logic busy);
  logic [W-1:0] mem [DEPTH];
  logic [AW:0] wptr, rptr;
  assign full  = (wptr[AW-1:0] == rptr[AW-1:0]) && (wptr[AW] != rptr[AW]);
  assign empty = (wptr == rptr);
  always_ff @(posedge clk) if (wr_en && !full) mem[wptr[AW-1:0]] <= wdata;
  assign rdata = mem[rptr[AW-1:0]];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin wptr <= '0; rptr <= '0; end
    else begin
      if (wr_en && !full) wptr <= wptr + 1'b1;
      if (rd_en && !empty) rptr <= rptr + 1'b1;
    end
  typedef enum logic [1:0] {IDLE, RUN, FIN} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (start) nx = RUN;
      RUN:  if (done) nx = FIN;
      FIN:  nx = st_t'(wdata[1:0]);   // not a constant: the transition graph cannot be read, so reachability stays an obligation
      default: nx = IDLE;
    endcase
  end
  assign busy = (st == RUN);
endmodule
)";
const char* kLatch = "module top(input logic en, d, output logic q); always_comb if (en) q = d; endmodule\n";
const char* kTwoClocks = "module top(input logic clka, clkb, d, output logic q);\n  logic a;\n"
    "  always_ff @(posedge clka) a <= d;\n  always_ff @(posedge clkb) q <= a;\nendmodule\n";
struct Run { int rc; std::string text; sverity::json::Value report; };
Run runGate(const std::string& name, const std::string& source, const std::string& args, const std::string& src = "") {
    const auto sv = write((src.empty() ? name : src) + ".sv", source);   // the same source name keeps fingerprints comparable
    const auto json = (scratch() / (name + ".json")).string(), text = (scratch() / (name + ".txt")).string();
    const auto cmd = std::string(SVERITY_BIN) + " --no-color --top top --json " + json + " " + args + " " + sv + " >" + text + " 2>&1";
    const int rc = std::system(cmd.c_str());
    auto parsed = sverity::json::parse(slurp(json));
    CHECK(parsed.ok);
    return {WIFEXITED(rc) ? WEXITSTATUS(rc) : -1, slurp(text), std::move(parsed.value)};
}
std::string lastLine(const std::string& text) {
    auto end = text.find_last_not_of('\n');
    auto start = text.rfind('\n', end);
    return text.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start));
}
}
TEST(cli_gate_a_one_flop_exits_zero_and_states_tool_coverage) {
    auto r = runGate("gate_a", kOneFlop, "--deck lint --goal lint");
    CHECK_EQ(r.rc, 0);
    CHECK(r.text.find("tool coverage: ") != std::string::npos && r.text.find(" deck rules complete") != std::string::npos);
    CHECK_EQ(lastLine(r.text), std::string("PASS: 0 defects; 0 obligations, 0 not analysed (see report)"));
    CHECK_EQ(r.report["summary"]["analysis_coverage"].asString(), std::string("COMPLETE"));
}
TEST(cli_gate_b_obligations_and_gaps_exit_zero_with_counts) {
    auto r = runGate("gate_b", kFifoFsm, "--deck lint --goal lint");
    CHECK_EQ(r.rc, 0);
    const auto obligations = r.report["summary"]["unresolved"]["obligations"].asInt(), gaps = r.report["summary"]["unresolved"]["not_analysed"].asInt();
    CHECK(obligations > 0 && gaps > 0);
    CHECK_EQ(lastLine(r.text), "PASS: 0 defects; " + std::to_string(obligations) + " obligations, " + std::to_string(gaps) + " not analysed (see report)");
    bool listed = false;   // never silent: the findings are in the report with their kind and disposition
    for (const auto& d : r.report["diagnostics"].asArray())
        listed = listed || (d["rule_id"].asString() == "LNT-071" && d["disposition"].asString() == "OBLIGATION" &&
                            d["unresolved_kind"].asString() == "obligation" && d["obligation"].asString() == "fsm-reachability-liveness");
    CHECK(listed);
    CHECK(r.text.find("<NOT_ANALYSED>") != std::string::npos);
}
TEST(cli_gate_c_strict_blocks_on_obligations_and_gaps) {
    auto r = runGate("gate_c", kFifoFsm, "--deck lint --goal lint --exit-policy strict");
    CHECK((r.rc & 2) != 0);
    CHECK_EQ(r.report["summary"]["exit_code_gate"].asInt(), 0);
    CHECK((r.report["summary"]["exit_code_strict"].asInt() & 2) != 0);
}
TEST(cli_gate_d_established_and_heuristic_warnings_still_block) {
    auto r = runGate("gate_d", kLatch, "--deck lint --goal lint");   // LNT-013: an established WARNING
    CHECK((r.rc & 2) != 0);
    CHECK_EQ(r.report["summary"]["defects"].asInt(), 1);
    CHECK(lastLine(r.text).find("REVIEW_REQUIRED: 1 defects;") == 0);
}
TEST(cli_gate_e_cdc_unresolved_still_blocks) {
    auto r = runGate("gate_e", kTwoClocks, "--goal cdc");
    CHECK((r.rc & 2) != 0);
    bool unresolved = false;
    for (const auto& d : r.report["diagnostics"].asArray())
        if (d["rule_id"].asString().rfind("CDC-", 0) == 0 && d["confidence"].asString() == "unresolved") {
            unresolved = true;
            CHECK_EQ(d["disposition"].asString(), std::string("REVIEW_REQUIRED"));
        }
    CHECK(unresolved);
}
TEST(cli_gate_f_elaboration_error_is_still_incomplete) {
    auto r = runGate("gate_f", "module top(input clk, d, output logic q); always_ff @(posedge clk) q <= undefined_sig; endmodule\n", "--deck lint --goal lint");
    CHECK((r.rc & 4) != 0 && (r.rc & 1) != 0);
}
TEST(cli_gate_baseline_blocks_only_new_unresolved_findings) {
    auto first = runGate("gate_base", kFifoFsm, "--deck lint --goal lint");
    CHECK_EQ(first.rc, 0);
    const auto baseline = (scratch() / "gate_base.json").string();
    auto same = runGate("gate_base_same", kFifoFsm, "--deck lint --goal lint --baseline " + baseline, "gate_base");
    CHECK_EQ(same.rc, 0);                                               // every obligation and gap is in the baseline
    auto changed = runGate("gate_base_new", kFifoFsm, "--deck lint --goal lint --baseline " + (scratch() / "gate_a.json").string(), "gate_base");
    CHECK((changed.rc & 2) != 0);                                       // the one-flop baseline has none of them
    CHECK_EQ(run("--top top --baseline " + (scratch() / "missing.json").string() + " " + write("gate_x.sv", kOneFlop)), 64);
    // The new ones are marked in the console and the JSON; the known ones are not.
    CHECK(changed.text.find("NEW against baseline") != std::string::npos);
    CHECK(changed.report["summary"]["unresolved"]["new_against_baseline"].asInt() > 0);
    CHECK(same.text.find("NEW against baseline") == std::string::npos);
    CHECK_EQ(same.report["summary"]["unresolved"]["new_against_baseline"].asInt(), 0);
}
TEST(cli_gate_malformed_baseline_fails_loudly) {
    const auto sv = write("gate_malformed.sv", kOneFlop);
    CHECK_EQ(run("--top top --baseline " + write("base-no-list.json", R"({"schema":"sverity-report/1","diagnostics":{}})") + " " + sv), 64);
    CHECK_EQ(run("--top top --baseline " + write("base-no-diagnostics.json", R"({"schema":"sverity-report/1"})") + " " + sv), 64);
    CHECK_EQ(run("--top top --baseline " + write("base-no-fingerprint.json", R"({"schema":"sverity-report/1","diagnostics":[{"rule_id":"LNT-082"}]})") + " " + sv), 64);
    CHECK_EQ(run("--top top --baseline " + write("base-empty.json", R"({"schema":"sverity-report/1","diagnostics":[]})") + " --deck lint --goal lint " + sv), 0);
}
// ---- baseline mode: --write-baseline records the findings, --baseline makes the known ones non-gating
namespace {
const char* kOneLatch = "module top(input logic en, d, output logic q);\n  always_comb if (en) q = d;\nendmodule\n";
const char* kTwoLatches = "module top(input logic en, d, e2, d2, output logic q, r);\n  always_comb if (en) q = d;\n  always_comb if (e2) r = d2;\nendmodule\n";
Run capture(const std::string& args) {
    const auto text = (scratch() / "capture.txt").string();
    const int rc = std::system((std::string(SVERITY_BIN) + " --no-color " + args + " >" + text + " 2>&1").c_str());
    return {WIFEXITED(rc) ? WEXITSTATUS(rc) : -1, slurp(text), sverity::json::Value()};
}
}
TEST(cli_baseline_is_written_with_its_context_and_does_not_accept_the_run) {
    const auto base = (scratch() / "bl_write.base.json").string();
    auto r = runGate("bl_write", kOneLatch, "--deck lint --goal lint --write-baseline " + base);
    CHECK((r.rc & 2) != 0);   // writing is not accepting: this run still gates
    auto b = sverity::json::parse(slurp(base));
    CHECK(b.ok);
    CHECK_EQ(b.value["schema"].asString(), std::string("sverity-baseline/1"));
    CHECK_EQ(b.value["context"]["top"].asString(), std::string("top"));
    CHECK_EQ(b.value["context"]["deck"].asString(), std::string("lint"));
    CHECK_EQ(b.value["findings"].asArray().size(), r.report["diagnostics"].asArray().size());   // every finding, none waived here
    std::set<std::string> written, reported;
    for (const auto& f : b.value["findings"].asArray()) {
        CHECK(!f["key"].asString().empty() && f["line"].asInt() > 0);
        written.insert(f["fingerprint"].asString());
    }
    for (const auto& d : r.report["diagnostics"].asArray()) reported.insert(d["fingerprint"].asString());
    CHECK(written == reported);
    CHECK(!r.report["summary"]["baseline"].isObject());   // no baseline was read
}
TEST(cli_baseline_matches_through_moved_lines_and_blocking_findings_stop_gating) {
    const auto base = (scratch() / "bl_move.base.json").string();
    auto first = runGate("bl_move0", kOneLatch, "--deck lint --goal lint --write-baseline " + base, "bl_move");
    CHECK((first.rc & 2) != 0);
    // The same design with lines added above it: every position changed, no finding did.
    auto moved = runGate("bl_move1", std::string("// header\n// another line\n\n") + kOneLatch, "--deck lint --goal lint --baseline " + base, "bl_move");
    CHECK_EQ(moved.rc, 0);
    CHECK_EQ(moved.report["summary"]["defects"].asInt(), 0);
    const auto& counts = moved.report["summary"]["baseline"];
    CHECK_EQ(counts["new"].asInt(), 0);
    CHECK(counts["baselined"].asInt() > 0);
    CHECK(counts["matched_by"]["key"].asInt() > 0);          // matched without the line
    bool latch = false;
    for (const auto& d : moved.report["diagnostics"].asArray())
        if (d["rule_id"].asString() == "LNT-013") {
            latch = true;
            CHECK_EQ(d["severity"].asString(), std::string("WARNING"));   // still a WARNING, listed and counted
            CHECK_EQ(d["baseline"].asString(), std::string("BASELINED"));
        }
    CHECK(latch);
    CHECK(moved.text.find("BASELINED") != std::string::npos);
    CHECK_EQ(lastLine(moved.text).find("PASS: 0 defects;"), 0u);
    // The same file again matches on the fingerprint alone.
    auto same = runGate("bl_move2", kOneLatch, "--deck lint --goal lint --baseline " + base, "bl_move");
    CHECK_EQ(same.rc, 0);
    CHECK_EQ(same.report["summary"]["baseline"]["matched_by"]["key"].asInt(), 0);
}
TEST(cli_baseline_gates_on_a_new_finding_beside_baselined_ones) {
    const auto base = (scratch() / "bl_new.base.json").string();
    runGate("bl_new0", kOneLatch, "--deck lint --goal lint --write-baseline " + base, "bl_new");
    auto r = runGate("bl_new1", kTwoLatches, "--deck lint --goal lint --baseline " + base, "bl_new");
    CHECK((r.rc & 2) != 0);                                    // the second latch is new
    CHECK_EQ(r.report["summary"]["defects"].asInt(), 1);       // and the baselined one is not counted as a defect
    int fresh = 0, known = 0;
    for (const auto& d : r.report["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "LNT-013") continue;
        (d["baseline"].asString() == "NEW" ? fresh : known)++;
    }
    CHECK_EQ(fresh, 1);
    CHECK_EQ(known, 1);
    const auto& counts = r.report["summary"]["baseline"];
    CHECK(counts["entries"].asInt() > 0 && counts["new"].asInt() >= 1 && counts["baselined"].asInt() >= 1);
    CHECK(counts["fixed_since_baseline"].isInt());
    CHECK(r.text.find("  baseline   " + std::to_string(counts["new"].asInt()) + " new, " + std::to_string(counts["baselined"].asInt()) + " baselined") != std::string::npos);
    CHECK(lastLine(r.text).find("; " + std::to_string(counts["baselined"].asInt()) + " baselined (see report)") != std::string::npos);
    // A second finding of the same kind is new even where the baseline holds one of that kind.
    auto doubled = runGate("bl_new2", "module top(input logic en, d, output logic q);\n  always_comb if (en) q = d;\n  always_comb if (d) q = en;\nendmodule\n",
                           "--deck lint --goal lint --baseline " + base, "bl_new");
    int open = 0;
    for (const auto& d : doubled.report["diagnostics"].asArray())
        if (d["rule_id"].asString() == "LNT-013" && d["baseline"].asString() == "NEW") ++open;
    CHECK(open >= 1);
}
namespace {
// A module whose unreset register steers a comparison (LNT-054) and whose constant comparison (LNT-005) is INFO while the constant is 10.
std::string twoModules(const std::string& first, const std::string& second, const std::string& constant) {
    const std::string head = "module m1(input logic clk, rst_n, ack, input logic [3:0] d, input logic [2:0] l, output logic y, z);\n  logic [3:0] word_q;\n";
    const std::string mid = "  assign y = (word_q == 4'd3) ? 1'b1 : 1'b0;\n  localparam int D = " + constant + ";\n  assign z = (l >= (D - 8)) ? 1'b1 : 1'b0;\nendmodule\n";
    const std::string m2head = "module m2(input logic clk, rst_n, ack, input logic [3:0] d, input logic [2:0] l, output logic y, z);\n  logic [3:0] word_q;\n";
    const std::string m2tail = "  assign y = (word_q == 4'd3) ? 1'b1 : 1'b0;\n  assign z = l[0];\nendmodule\n";
    return head + first + mid + m2head + second + m2tail +
           "module top(input logic clk, rst_n, ack, input logic [3:0] d, input logic [2:0] l, output logic [1:0] y, z);\n"
           "  m1 u1(.clk, .rst_n, .ack, .d, .l, .y(y[0]), .z(z[0]));\n  m2 u2(.clk, .rst_n, .ack, .d, .l, .y(y[1]), .z(z[1]));\nendmodule\n";
}
const char* kUnreset = "  always_ff @(posedge clk) if (ack) word_q <= d;\n";
const char* kReset = "  always_ff @(posedge clk or negedge rst_n) if (!rst_n) word_q <= '0; else if (ack) word_q <= d;\n";
}
TEST(cli_baseline_does_not_cover_a_finding_that_became_more_severe_at_the_same_place) {
    const auto base = (scratch() / "bl_sev.base.json").string();
    runGate("bl_sev0", twoModules(kReset, kReset, "10"), "--deck lint --goal lint --write-baseline " + base, "bl_sev");
    // The constant now makes the comparison wrong: the same rule at the same line is a WARNING where the baseline holds an INFO.
    auto worse = runGate("bl_sev1", twoModules(kReset, kReset, "6"), "--deck lint --goal lint --baseline " + base, "bl_sev");
    bool seen = false;
    for (const auto& d : worse.report["diagnostics"].asArray())
        if (d["rule_id"].asString() == "LNT-005" && d["severity"].asString() == "WARNING") {
            seen = true;
            CHECK_EQ(d["baseline"].asString(), std::string("NEW"));
        }
    CHECK(seen);
    CHECK((worse.rc & 2) != 0);
    // At or below its recorded severity a finding stays covered.
    auto same = runGate("bl_sev2", twoModules(kReset, kReset, "10"), "--deck lint --goal lint --baseline " + base, "bl_sev");
    CHECK_EQ(same.rc, 0);
}
TEST(cli_baseline_does_not_carry_a_finding_into_another_module) {
    const auto base = (scratch() / "bl_scope.base.json").string();
    // The unreset register is in m1 when the baseline is written, and in m2 afterwards (m1 was fixed).
    runGate("bl_scope0", twoModules(kUnreset, kReset, "10"), "--deck lint --goal lint --write-baseline " + base, "bl_scope");
    auto moved = runGate("bl_scope1", twoModules(kReset, kUnreset, "10"), "--deck lint --goal lint --baseline " + base, "bl_scope");
    int fresh = 0;
    for (const auto& d : moved.report["diagnostics"].asArray())
        if (d["rule_id"].asString() == "LNT-054") {
            CHECK_EQ(d["baseline"].asString(), std::string("NEW"));   // same rule, file and signal name, another module
            ++fresh;
        }
    CHECK_EQ(fresh, 1);
    CHECK((moved.rc & 2) != 0);
    CHECK(moved.report["summary"]["baseline"]["fixed_since_baseline"].asInt() >= 1);   // the m1 finding is no longer present
    // In its own module a finding that moved by a few lines is still the baselined one.
    auto shifted = runGate("bl_scope2", std::string("// one\n// two\n") + twoModules(kUnreset, kReset, "10"), "--deck lint --goal lint --baseline " + base, "bl_scope");
    CHECK_EQ(shifted.rc, 0);
}
TEST(cli_baseline_context_names_the_elaborated_top_not_only_the_argument) {
    const auto one = write("bl_top1.sv", "module top(input logic a, output logic y);\n  assign y = a;\nendmodule\n");
    const auto two = write("bl_top2.sv", "module top(input logic a, output logic y);\n  assign y = a;\nendmodule\nmodule extra(input logic a, output logic y);\n  assign y = a;\nendmodule\n");
    const auto base = (scratch() / "bl_top.base.json").string();
    CHECK_EQ(capture("--deck lint --goal lint --write-baseline " + base + " " + one).rc & 12, 0);
    auto b = sverity::json::parse(slurp(base));
    CHECK_EQ(b.value["context"]["top"].asString(), std::string("top"));   // no --top: the top the design elaborated to
    auto other = capture("--deck lint --goal lint --baseline " + base + " " + two);   // two tops now: not the design the baseline was written for
    CHECK_EQ(other.rc, 64);
    CHECK(other.text.find("top 'top' (this run: 'extra,top')") != std::string::npos);
    CHECK_EQ(capture("--deck lint --goal lint --baseline " + base + " " + one).rc, 0);
}
TEST(cli_baseline_for_another_top_or_deck_is_rejected) {
    const auto src = write("bl_ctx.sv", std::string(kOneLatch) + "module other(input logic en, d, output logic q);\n  always_comb if (en) q = d;\nendmodule\n");
    const auto base = (scratch() / "bl_ctx.base.json").string();
    CHECK_EQ(capture("--top top --deck lint --goal lint --write-baseline " + base + " " + src).rc & 12, 0);
    auto top = capture("--top other --deck lint --goal lint --baseline " + base + " " + src);
    CHECK_EQ(top.rc, 64);
    CHECK(top.text.find("top 'top' (this run: 'other')") != std::string::npos);
    auto deck = capture("--top top --goal lint --baseline " + base + " " + src);
    CHECK_EQ(deck.rc, 64);
    CHECK(deck.text.find("deck 'lint' (this run: '(none)')") != std::string::npos);
    CHECK_EQ(capture("--top top --deck lint --goal lint --baseline " + base + " " + src).rc, 0);   // the same context is fine
    // A baseline that cannot be read exactly is an error, not an empty baseline.
    CHECK_EQ(run("--top top --baseline " + write("bl_bad.json", R"({"schema":"sverity-baseline/1","context":{"top":"top","deck":""}})") + " " + src), 64);
    CHECK_EQ(run("--top top --baseline " + write("bl_bad2.json", R"({"schema":"sverity-baseline/1","context":{"top":"top","deck":""},"findings":[{"rule_id":"LNT-013"}]})") + " " + src), 64);
}
TEST(cli_gate_strict_policy_is_what_json_and_sarif_state) {
    const auto sarif = (scratch() / "gate_policy.sarif").string();
    auto strict = runGate("gate_policy_strict", kFifoFsm, "--deck lint --goal lint --exit-policy strict --sarif " + sarif);
    CHECK_EQ(strict.report["summary"]["review_status"].asString(), std::string("REVIEW_REQUIRED"));
    CHECK_EQ(strict.report["inputs"]["exit_policy"].asString(), std::string("strict"));
    auto parsed = sverity::json::parse(slurp(sarif));
    CHECK(parsed.ok);
    CHECK_EQ(parsed.value["runs"].asArray()[0]["properties"]["reviewStatus"].asString(), std::string("REVIEW_REQUIRED"));
    // Without a deck the catalog has partial rules, which strict counts: coverage is stated under the policy too.
    auto catalog = runGate("gate_policy_catalog", kOneFlop, "--goal lint --exit-policy strict");
    CHECK_EQ(catalog.report["summary"]["analysis_coverage"].asString(), std::string("INCOMPLETE"));
    auto gate = runGate("gate_policy_gate", kFifoFsm, "--deck lint --goal lint");
    CHECK_EQ(gate.report["summary"]["review_status"].asString(), std::string("PASS"));
    CHECK_EQ(gate.report["inputs"]["exit_policy"].asString(), std::string("gate"));
}
TEST(cli_gate_a_known_range_is_a_finding_and_only_a_free_input_is_an_obligation) {
    const char* source = R"(module top(input logic [7:0] d, input logic [2:0] free, input logic [3:0] n, output logic [7:0] q, output logic [7:0] r);
  always_comb begin
    q = '0;
    for (int i = 0; i < 4; i++) q = q + d / 8'(i);
    for (int j = 0; j < 3; j++) q[j ** 2] = d[j];
  end
  logic [5:0] data;
  assign r = {7'b0, data[free]} / n;
endmodule
)";
    auto out = runGate("gate_range", source, "--deck lint --goal lint --option LNT-006.dynamic_index=true --option LNT-037.dynamic_divisor=true");
    bool knownDivisor = false, gap = false, freeIndex = false, freeDivisor = false;
    for (const auto& d : out.report["diagnostics"].asArray()) {
        const auto rule = d["rule_id"].asString(), kind = d["unresolved_kind"].asString(), msg = d["message"].asString();
        if (rule == "LNT-037" && msg.find("'8'(i)'") != std::string::npos) {
            knownDivisor = d["disposition"].asString() == "REVIEW_REQUIRED" && d["confidence"].asString() == "heuristic";
        }
        if (rule == "LNT-006" && msg.find("j ** 2") != std::string::npos) gap = kind == "gap" && d["obligation"].asString().empty();
        if (rule == "LNT-006" && msg.find("'free'") != std::string::npos) freeIndex = d["obligation"].asString() == "index-input-contract" && kind == "obligation";
        if (rule == "LNT-037" && msg.find("'n'") != std::string::npos) freeDivisor = d["obligation"].asString() == "divisor-input-contract" && kind == "obligation";
    }
    CHECK(knownDivisor && gap && freeIndex && freeDivisor);
    CHECK((out.rc & 2) != 0);   // the divisor that is zero in its first iteration blocks
    // An FSM relation the inventory cannot attribute (state in an array) is a tool gap, not an obligation.
    auto fsm = runGate("gate_fsm_array", slurp(std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/LNT-029/unknown_array_selector_state.sv"), "--deck lint --goal lint");
    bool unattributed = false;
    for (const auto& d : fsm.report["diagnostics"].asArray())
        if (d["rule_id"].asString() == "LNT-029" && d["confidence"].asString() == "unresolved") {
            unattributed = true;
            CHECK_EQ(d["unresolved_kind"].asString(), std::string("gap"));
            CHECK(d["obligation"].isNull());
        }
    CHECK(unattributed);
}
TEST(cli_gate_uninstantiated_definitions_are_listed_in_the_coverage_output) {
    auto out = runGate("gate_uninst", "module top(input logic [7:0] a, output logic [7:0] y); assign y = a; endmodule\n"
                       "module orphan(input logic [7:0] a, output logic [3:0] y); assign y = a; endmodule\n", "--deck lint --goal lint");
    const auto& listed = out.report["coverage"]["uninstantiated_definitions"].asArray();
    CHECK_EQ(listed.size(), 1u);
    CHECK(!listed.empty() && listed[0].asString() == "orphan");
    CHECK(out.text.find("not linted  1 definition(s) the top does not instantiate: orphan") != std::string::npos);
    CHECK_EQ(out.report["coverage"]["design_objects"]["uninstantiated_definitions"].asInt(), 1);
}
TEST(cli_gate_a_black_box_stays_incomplete_when_the_cst_rules_are_off) {
    const char* source = "(* blackbox *) module opaque(input logic c, output logic y); assign y = c; endmodule\n"
        "module top(input logic clk, d, output logic q, observed);\n  always_ff @(posedge clk) q <= d;\n  opaque u(clk, observed);\nendmodule\n";
    auto on = runGate("gate_bb_on", source, "--deck lint --goal lint");
    auto off = runGate("gate_bb_off", source, "--deck lint --goal lint --disable-rule CST-*");
    for (const auto* r : {&on, &off}) {
        CHECK((r->rc & 4) != 0);
        bool scope = false;
        for (const auto& s : r->report["coverage"]["scopes"].asArray())
            scope = scope || (s["scope"].asString() == "blackboxes" && s["analysis_coverage"].asString() == "INCOMPLETE");
        CHECK(scope);
    }
}
TEST(cli_gate_called_write_whose_value_is_not_established_degrades_the_style_rules) {
    const char* source = R"(module top(input logic [7:0] x, input logic [3:0] n, output logic [7:0] y);
  task automatic t(input logic [7:0] v, input logic [3:0] k, output logic [7:0] o);
    logic [7:0] acc;
    acc = '0;
    for (logic [3:0] i = 0; i < k; i++) acc = acc + v;
    o = acc;
  endtask
  always_comb t(x, n, y);
endmodule
)";
    auto out = runGate("gate_copyback", source, "--deck lint --goal lint");
    CHECK((out.rc & 4) != 0);
    for (const auto& rule : out.report["coverage"]["rules"].asArray())
        if (rule["rule_id"].asString() == "LNT-062" || rule["rule_id"].asString() == "LNT-063")
            CHECK_EQ(rule["state"].asString(), std::string("PARTIAL"));
}
TEST(cli_gate_a_missing_sensitivity_entry_gates_beside_an_uncertain_one) {
    const char* source = R"(module top(input logic [3:0] a, input logic [1:0] sel, input logic b, output logic y);
  always @(a[sel] or sel) y = a[0] | b;
endmodule
)";
    auto out = runGate("gate_sens", source, "--deck lint --goal lint");
    CHECK((out.rc & 2) != 0);
    bool certain = false, uncertain = false;
    for (const auto& d : out.report["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "LNT-045" || d["severity"].asString() != "WARNING") continue;
        const auto msg = d["message"].asString();
        if (msg.find("reads 'b'") != std::string::npos) certain = d["disposition"].asString() == "REVIEW_REQUIRED";
        if (msg.find("reads 'a'") != std::string::npos) uncertain = d["disposition"].asString() == "NOT_ANALYSED";
    }
    CHECK(certain && uncertain);
}
TEST(cli_html_detail_row_spans_every_column_and_lists_no_known_disposition) {
    const auto sv = write("gate_html.sv", kFifoFsm), html = (scratch() / "gate.html").string();
    CHECK_EQ(run("--top top --deck lint --goal lint --html " + html + " " + sv), 0);
    const auto text = slurp(html);
    CHECK(text.find("colspan=\"16\"") != std::string::npos);
    for (size_t at = 0; (at = text.find("colspan=\"16\">", at)) != std::string::npos; ++at) {   // the dispositions are columns, not extras
        const auto row = text.substr(at, text.find("</td>", at) - at);
        CHECK(row.find("OBLIGATION") == std::string::npos && row.find("NOT_ANALYSED") == std::string::npos);
    }
}
TEST(cli_malformed_waiver_file_reports_cst006_error_established) {
    const auto sv = write("cst006_bad_file.sv",
                          "module top(input logic clk, d, output logic q);\n"
                          "  always_ff @(posedge clk) q <= d;\nendmodule\n");
    const auto sdc = write("cst006_bad_file.sdc",
                           "create_clock -name clk -period 10 [get_ports clk]\n"
                           "set_input_delay -clock clk 0 [get_ports d]\n");
    const auto badWaiver = write("cst006_bad_file.waivers.json", "{\"waivers\": [");
    const auto out = (scratch() / "cst006_bad_file.json").string();
    const int rc = run("--profile asic --goal cdc --deck cdc --top top --sdc " + sdc + " --waivers " + badWaiver + " --json " + out + " " + sv);
    CHECK((rc & 8) != 0);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    CHECK_EQ(j.value["summary"]["execution_status"].asString(), std::string("ERROR"));
    bool seen = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() == "CST-006" && d["severity"].asString() == "ERROR" &&
            d["confidence"].asString() == "established")
            seen = true;
    }
    CHECK(seen);
}

TEST(cli_cst004_unknown_clock_pair_populates_signals) {
    const auto hasSignal = [](const auto& d, std::string_view wanted) {
        for (const auto& s : d["signals"].asArray())
            if (s.asString() == wanted) return true;
        return false;
    };
    const auto sv = write("cst004_sigs.sv",
                          "module top(input logic clk_a, clk_b, d, output logic q);\n"
                          "  logic s_q;\n"
                          "  always_ff @(posedge clk_a) s_q <= d;\n"
                          "  always_ff @(posedge clk_b) q <= s_q;\n"
                          "endmodule\n");
    const auto sdc = write("cst004_sigs.sdc",
                           "create_clock -name sys_clk -period 10 [get_ports clk_a]\n"
                           "create_clock -name clk_b -period 8 [get_ports clk_b]\n"
                           "set_input_delay -clock sys_clk 1 [get_ports d]\n");
    const auto out = (scratch() / "cst004_sigs.json").string();
    const int rc = run("--profile asic --goal cdc --deck cdc --top top --sdc " + sdc + " --json " + out + " " + sv);
    CHECK((rc & 8) == 0);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool seen = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CST-004") continue;
        seen = true;
        CHECK(hasSignal(d, "top.clk_a"));
        CHECK(hasSignal(d, "sys_clk"));
        CHECK(hasSignal(d, "top.clk_b"));
    }
    CHECK(seen);
}

TEST(cli_cst009_populates_signals_from_command_and_targets) {
    const auto hasSignal = [](const auto& d, std::string_view wanted) {
        for (const auto& s : d["signals"].asArray())
            if (s.asString() == wanted) return true;
        return false;
    };
    const auto sv = write("cst009_sigs.sv",
                          "module top(input logic clk_src, clk_dst, flag_i, en_i, output logic flag_o);\n"
                          "  logic src_q, s1_q, s2_q;\n"
                          "  always_ff @(posedge clk_src) src_q <= flag_i & en_i;\n"
                          "  always_ff @(posedge clk_dst) begin s1_q <= src_q; s2_q <= s1_q; end\n"
                          "  assign flag_o = s2_q;\n"
                          "endmodule\n");
    const auto sdc = write("cst009_sigs.sdc",
                           "create_clock -name clk_src -period 20 [get_ports clk_src]\n"
                           "create_clock -name clk_dst -period 8 [get_ports clk_dst]\n"
                           "set_clock_groups -asynchronous -group {clk_src} -group {clk_dst}\n"
                           "set_input_delay -clock clk_src 1 [get_ports flag_i]\n"
                           "set_clock_latency -source 0.5 [get_clocks clk_dst]\n"
                           "set_load 0.1 [get_ports -filter {name =~ d*}]\n"
                           "set_input_transition 0.2 [get_ports -filter \"direction == in\"]\n"
                           "set_drive 1 [get_ports -regexp {d.+}]\n"
                           "set_case_analysis 1 [get_nets src_q]\n");
    const auto out = (scratch() / "cst009_sigs.json").string();
    const int rc = run("--profile asic --goal cdc --deck cdc --top top --sdc " + sdc + " --json " + out + " " + sv);
    CHECK((rc & 8) == 0);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool latencySeen = false, caseSeen = false, loadSeen = false, transSeen = false, driveSeen = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CST-009") continue;
        const auto& sigs = d["signals"].asArray();
        if (hasSignal(d, "set_clock_latency")) {
            latencySeen = true;
            CHECK(hasSignal(d, "clk_dst"));
        }
        if (hasSignal(d, "set_case_analysis")) {
            caseSeen = true;
            CHECK(hasSignal(d, "src_q"));
        }
        if (hasSignal(d, "set_load")) {
            loadSeen = true;
            CHECK_EQ(sigs.size(), 1u);
        }
        if (hasSignal(d, "set_input_transition")) {
            transSeen = true;
            CHECK_EQ(sigs.size(), 1u);
        }
        if (hasSignal(d, "set_drive")) {
            driveSeen = true;
            CHECK_EQ(sigs.size(), 1u);
        }
    }
    CHECK(latencySeen);
    CHECK(caseSeen);
    CHECK(loadSeen);
    CHECK(transSeen);
    CHECK(driveSeen);
}

TEST(cli_cdc043_dual_edge_populates_signals_and_deduplicates_vector) {
    const auto sv = write("cdc043_sigs.sv",
                          "module top(input logic clk, input logic [3:0] d, output logic [3:0] q);\n"
                          "  logic [3:0] ddr_q, split_q;\n"
                          "  always @(posedge clk or negedge clk) ddr_q <= d;\n"
                          "  always @(posedge clk or negedge clk) split_q[1:0] <= d[1:0];\n"
                          "  always @(posedge clk or negedge clk) split_q[3:2] <= d[3:2];\n"
                          "  always_ff @(posedge clk) q <= ddr_q ^ split_q;\n"
                          "endmodule\n");
    const auto sdc = write("cdc043_sigs.sdc",
                           "create_clock -name clk -period 10 [get_ports clk]\n");
    const auto out = (scratch() / "cdc043_sigs.json").string();
    const int rc = run("--profile asic --goal cdc --deck cdc --top top --sdc " + sdc + " --json " + out + " " + sv);
    CHECK((rc & 8) == 0);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    int ddrCount = 0, splitCount = 0;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CDC-043") continue;
        const auto& sigs = d["signals"].asArray();
        CHECK_EQ(sigs.size(), 1u);
        if (sigs[0].asString() == "top.ddr_q") ++ddrCount;
        else if (sigs[0].asString() == "top.split_q") ++splitCount;
    }
    CHECK_EQ(ddrCount, 1);
    CHECK_EQ(splitCount, 1);
}

TEST(cli_reset_contracts_config) {
    const auto fixtures = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-013/";
    const auto sv = fixtures + "boundary_input_sync.sv";
    const auto out = (scratch() / "reset-contracts.json").string();
    const auto bad = write("reset-contracts-bad.json", R"({"reset_contracts":{"sources":[{"id":"por","port":"top.por_n","active":"mid"}],"state":[{"id":"s","source":"por","initial":"asserted"}]}})");
    CHECK_EQ(run("--top top --config " + bad + " " + sv), 64);
    const auto good = write("reset-contracts-good.json", R"({"reset_contracts":{"sources":[{"id":"por","port":"top.event_i","active":"low"}],"state":[{"id":"s","source":"por","initial":"asserted"}]}})");
    run("--top top --json " + out + " --config " + good + " " + sv);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    CHECK_EQ(j.value["inputs"]["reset_contracts"]["sources"].asArray().size(), std::size_t(1));
    CHECK_EQ(j.value["inputs"]["reset_contracts"]["state"].asArray()[0]["initial"].asString(), std::string("asserted"));
}

TEST(cli_rdc005_cites_assert_together_group_without_severity_change) {
    const auto sv = write("rdc005-group.sv", R"(module top(input logic clka, clkb, rst_a, rst_b, d, output logic q);
  logic s, t1, t2;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) s <= 1'b0; else s <= d;
  always_ff @(posedge clkb or negedge rst_b) if (!rst_b) {t2, t1} <= 2'b0; else {t2, t1} <= {t1, s};
  assign q = t2;
endmodule
)");
    const auto cfg = write("rdc005-group.json", R"({"reset_contracts":{"sources":[{"id":"a","port":"top.rst_a","active":"low"},{"id":"b","port":"top.rst_b","active":"low"}],"assert_together":[{"id":"g_ab","sources":["a","b"]}]}})");
    const auto sdc = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc";
    const auto out = (scratch() / "rdc005-group-report.json").string();
    run("--top top --sdc " + sdc + " --config " + cfg + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool found = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "RDC-005") continue;
        found = true;
        CHECK_EQ(d["severity"].asString(), std::string("WARNING"));
        bool cited = false;
        for (const auto& ev : d["evidence"].asArray()) cited = cited || ev["text"].asString() == "g_ab (assert_together, assumed)";
        CHECK(cited);
    }
    CHECK(found);
}

TEST(cli_rdc011_declared_port_order_is_marked_environment_assumption) {
    const auto sv = write("rdc011-order.sv", R"(module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= q;
endmodule
)");
    const auto cfg = write("rdc011-order.json", R"({"reset_contracts":{"sources":[{"id":"a","port":"top.rst_a_n","active":"low"},{"id":"b","port":"top.rst_b_n","active":"low"}],"release_order":[{"id":"a_before_b","before":"a","after":"b","clock":"clka","edge":"posedge","min_cycles":2}]}})");
    const auto sdc = std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc";
    const auto out = (scratch() / "rdc011-order-report.json").string();
    run("--top top --sdc " + sdc + " --config " + cfg + " --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool found = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "RDC-011") continue;
        found = true;
        CHECK_EQ(d["severity"].asString(), std::string("WARNING"));
        CHECK(d["message"].asString().find("(declared by the environment, not checked)") != std::string::npos);
    }
    CHECK(found);
}

TEST(cli_rdc019_lists_functional_user_register_in_signals) {
    const auto sv = write("rdc019-signals.sv", R"(module top(input logic clk, rst_ni, d, output logic q, in_rst_q);
  always_ff @(posedge clk or negedge rst_ni) if (!rst_ni) q <= 1'b0; else q <= d;
  always_ff @(posedge clk) in_rst_q <= ~rst_ni;
endmodule
)");
    const auto out = (scratch() / "rdc019-signals.json").string();
    run("--top top --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool found = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "RDC-019") continue;
        found = true;
        bool hasRoot = false, hasUser = false;
        for (const auto& s : d["signals"].asArray()) {
            if (s.asString() == "top.rst_ni") hasRoot = true;
            if (s.asString() == "top.in_rst_q") hasUser = true;
        }
        CHECK(hasRoot);
        CHECK(hasUser);
    }
    CHECK(found);
}

TEST(cli_cst005_lists_blackbox_output_nets_and_driven_state) {
    const auto sv = write("cst005-signals.sv", R"((* black_box *) module bb_ff(input logic ck, input logic d, output logic q);
endmodule
module top(input logic clk, d, output logic status_o);
  logic status_q;
  bb_ff u_bb(.ck(clk), .d(d), .q(status_q));
  assign status_o = status_q;
endmodule
)");
    const auto out = (scratch() / "cst005-signals.json").string();
    run("--top top --json " + out + " " + sv);
    auto j = sverity::json::parse(slurp(out));
    CHECK(j.ok);
    bool found = false;
    for (const auto& d : j.value["diagnostics"].asArray()) {
        if (d["rule_id"].asString() != "CST-005") continue;
        found = true;
        bool hasNet = false, hasPort = false;
        for (const auto& s : d["signals"].asArray()) {
            if (s.asString() == "top.status_q") hasNet = true;
            if (s.asString() == "top.status_o") hasPort = true;
        }
        CHECK(hasNet);
        CHECK(hasPort);
    }
    CHECK(found);
}
