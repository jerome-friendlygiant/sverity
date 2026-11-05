#include <filesystem>
#include <fstream>
#include <map>

#include "sverity/waiver.h"
#include "testing.h"
#include "sverity/analyzer.h"
using namespace sverity;

static Report base() {
    Report r;
    r.addRuleCoverage({"CDC-003", RuleState::Executed, "implemented", "", 1, 0});
    Diagnostic d;
    d.ruleId = "CDC-003"; d.severity = Severity::Error; d.message = "async crossing";
    d.location = {"rtl/a.sv", 10, 3}; d.hierarchy = "top.u_a"; d.signals = {"top.sig_q"};
    r.add(d);
    return r;
}
static WaiverSet parse(const char* text) {
    WaiverSet w; std::string err;
    CHECK(WaiverSet::parse(text, w, err));
    return w;
}
static int count(const Report& r, const char* rule) {
    int n = 0; for (auto& d : r.diagnostics()) if (d.ruleId == rule) n++; return n;
}

TEST(waiver_keeps_severity_and_finding) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[{"id":"W1","rule":"CDC-003","file":"rtl/a.sv","line":10,
        "reason":"static config","owner":"jo","expires":"2027-01-01"}]})"), "2026-11-01");
    auto& d = r.diagnostics().at(0);
    CHECK_EQ(d.disposition, Disposition::Waived);
    CHECK_EQ(d.severity, Severity::Error);            // never rewritten
    CHECK(d.waiver.has_value());
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Pass);   // waived finding no longer fails the run...
    CHECK_EQ(r.countDisposition(Disposition::Waived), 1u);   // ...but is counted and still emitted
    CHECK_EQ(r.toJson()["diagnostics"].asArray().size(), 1u);
}
TEST(expired_waiver_not_applied_and_warned) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[{"id":"W1","rule":"CDC-003","file":"rtl/a.sv",
        "reason":"x","owner":"jo","expires":"2026-01-01"}]})"), "2026-11-01");
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
    CHECK_EQ(count(r, "CST-022"), 1);
}
TEST(overbroad_waiver_not_applied) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[{"id":"W1","rule":"CDC-003","file":"*","reason":"x","owner":"jo"}]})"),
                 "2026-11-01");
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
    CHECK_EQ(count(r, "CST-022"), 1);
}
TEST(waiver_without_owner_or_reason_invalid) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[{"id":"W1","rule":"CDC-003","file":"rtl/a.sv","reason":"x"}]})"),
                 "2026-11-01");
    CHECK_EQ(count(r, "CST-006"), 1);
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
}
TEST(stale_waiver_flagged) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[{"id":"W1","rule":"CDC-003","file":"rtl/other.sv","reason":"x","owner":"jo"}]})"),
                 "2026-11-01");
    CHECK_EQ(count(r, "CST-022"), 1);
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
}
TEST(glob_matching) {
    CHECK(globMatch("top.*.u_a", "top.x.y.u_a"));
    CHECK(!globMatch("top.u_?", "top.u_ab"));
    CHECK(globMatch("*", ""));
}

TEST(waiver_schema_is_strict) {
    for (const char* bad : {
             R"({"waivers":[{"rule":"CDC-003","file":"rtl/a.sv","reason":"x","owner":"o","mesage_contains":"typo"}]})",
             R"({"waivers":[{"rule":"CDC-003","file":"rtl/a.sv","line":"10","reason":"x","owner":"o"}]})",
             R"({"waivers":[{"rule":"CDC-003","file":"rtl/a.sv","line":1e300,"reason":"x","owner":"o"}]})",
             R"({"waivers":[{"rule":"CDC-003","file":"rtl/a.sv","line":4294967298,"reason":"x","owner":"o"}]})"}) {
        Report r = base();
        applyWaivers(r, parse(bad), "2026-11-01");
        CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);     // nothing was waived
        CHECK_EQ(count(r, "CST-006"), 1);
    }
}
TEST(duplicate_waiver_ids_rejected) {
    Report r = base();
    applyWaivers(r, parse(R"({"waivers":[
      {"id":"W","rule":"CDC-003","file":"rtl/a.sv","reason":"x","owner":"o"},
      {"id":"W","rule":"CDC-003","file":"rtl/b.sv","reason":"x","owner":"o"}]})"), "2026-11-01");
    CHECK_EQ(count(r, "CST-006"), 2); // both duplicate IDs are invalid, including the first
    CHECK_EQ(r.countDisposition(Disposition::Waived), 0u);
}
TEST(date_validation) {
    CHECK(isValidIsoDate("2026-02-28"));
    CHECK(!isValidIsoDate("2026-02-29"));
    CHECK(isValidIsoDate("2028-02-29"));
    CHECK(!isValidIsoDate("2020-99-99"));
    CHECK(!isValidIsoDate("1"));
}
TEST(hierarchy_waiver_applies_per_occurrence) {
    Report r;
    r.addRuleCoverage({"CDC-003", RuleState::Executed, "implemented", "", 1, 0});
    for (const char* h : {"top.u1", "top.u2"}) {
        Diagnostic d;
        d.ruleId = "CDC-003"; d.severity = Severity::Error; d.message = "m"; d.location = {"a.sv", 5, 1}; d.hierarchy = h;
        r.add(d);
    }
    applyWaivers(r, parse(R"({"waivers":[{"id":"W","rule":"CDC-003","hierarchy":"top.u1","reason":"x","owner":"o"}]})"),
                 "2026-11-01");
    CHECK_EQ(r.countDisposition(Disposition::Waived), 1u);
    CHECK_EQ(r.countDisposition(Disposition::Open), 1u);        // top.u2 is still an open ERROR
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
}
TEST(hierarchy_waiver_not_applied_to_coalesced_findings) {
    Report r;
    r.addRuleCoverage({"LNT-004", RuleState::Executed, "implemented", "", 1, 0});
    Diagnostic d;
    d.ruleId = "LNT-004"; d.severity = Severity::Warning; d.message = "m"; d.location = {"a.sv", 5, 1};
    d.hierarchy = "top.u1";
    d.evidence.push_back({"occurrence_list_incomplete", "coalesced", {}});
    r.add(d);
    applyWaivers(r, parse(R"({"waivers":[{"id":"W","rule":"LNT-004","hierarchy":"top.u1","reason":"x","owner":"o"}]})"),
                 "2026-11-01");
    CHECK_EQ(r.countDisposition(Disposition::Waived), 0u);
    CHECK_EQ(count(r, "CST-022"), 1);
}

static Report sourceReport(const char* name, bool enabled = true) {
    AnalyzerOptions o;
    std::string root = SVERITY_SOURCE_DIR;
    o.pathRoot = root;
    o.frontend.slangArgs = {"--top", "top", root + "/tests/fixtures/CST-006/" + name + ".sv"};
    o.sdcFiles = {root + "/tests/fixtures/_common/async.sdc"};
    o.today = "2026-11-01";
    o.sourceWaivers = enabled;
    return analyze(o);
}
TEST(source_waiver_valid_and_formats) {
    auto r = sourceReport("source_valid");
    CHECK_EQ(r.countDisposition(Disposition::Waived), 1u);
    CHECK_EQ(count(r, "CST-022"), 0);
    CHECK(r.toJson().dump().find("source_location") != std::string::npos);
    CHECK(r.toSarif().dump().find("source_valid.sv") != std::string::npos);
    CHECK(r.toHtml().find("WAIVED") != std::string::npos);
    CHECK(r.toText(false).find("source_valid.sv:") != std::string::npos);
}
#define SOURCE_REJECT_TEST(name, rule) TEST(name) { auto r = sourceReport(#name); CHECK_EQ(count(r, rule), 1); CHECK_EQ(r.countDisposition(Disposition::Waived), 0u); }
SOURCE_REJECT_TEST(source_missing_owner, "CST-006")
SOURCE_REJECT_TEST(source_wrong_rule, "CST-006")
SOURCE_REJECT_TEST(source_malformed, "CST-006")
SOURCE_REJECT_TEST(source_expired, "CST-022")
SOURCE_REJECT_TEST(source_stale, "CST-022")
TEST(source_waiver_multiline_procedure_location) {
    auto r = sourceReport("source_multiline");
    bool cdc28 = false;
    for (const auto& d : r.diagnostics()) cdc28 |= d.disposition == Disposition::Waived && d.ruleId == "CDC-028";
    CHECK(cdc28); CHECK_EQ(count(r, "CST-022"), 1);   // the assignment-line directive stays stale
}
TEST(source_waivers_can_be_disabled) {
    auto r = sourceReport("source_valid", false);
    CHECK_EQ(r.countDisposition(Disposition::Waived), 0u);
    CHECK(!r.toJson()["inputs"]["source_waivers"].asBool());
}
TEST(source_waiver_fingerprint_unchanged) {
    auto a = sourceReport("source_valid"), b = sourceReport("source_valid", false);
    for (const auto& d : a.diagnostics()) if (d.ruleId == "LNT-076")
        for (const auto& e : b.diagnostics()) if (e.ruleId == d.ruleId) CHECK_EQ(d.fingerprint, e.fingerprint);
}
TEST(source_waiver_coverage_unchanged) {
    auto a = sourceReport("source_valid"), b = sourceReport("source_valid", false);
    CHECK_EQ(a.analysisCoverage(), b.analysisCoverage());
    CHECK_EQ(a.toJson()["coverage"]["scopes"].dump(), b.toJson()["coverage"]["scopes"].dump());
}
TEST(template_round_trip_rejects_every_placeholder) {
    auto r = base(); r.finalize();
    auto t = waiverTemplate(r);
    WaiverSet set; std::string err;
    CHECK(WaiverSet::parse(t.dump(), set, err));
    applyWaivers(r, set, "2026-11-01");
    CHECK_EQ(r.countDisposition(Disposition::Waived), 0u);
    CHECK_EQ(count(r, "CST-006"), 1);
}
TEST(template_filled_fields_work) {
    auto r = base(); r.finalize();
    auto set = parse(waiverTemplate(r).dump().c_str());
    set.mutableWaivers()[0].owner = "team"; set.mutableWaivers()[0].reason = "reviewed protocol";
    applyWaivers(r, set, "2026-11-01");
    CHECK_EQ(r.countDisposition(Disposition::Waived), 1u);
}
TEST(waiver_cannot_hide_frontend_errors_or_fatal) {
    for (bool fatal : {false, true}) {
        auto r = base(); auto& d = r.mutableDiagnostics()[0];
        d.origin = "slang"; d.severity = fatal ? Severity::Fatal : Severity::Error;
        applyWaivers(r, parse(R"({"waivers":[{"rule":"CDC-003","file":"rtl/a.sv","reason":"reviewed","owner":"team"}]})"), "2026-11-01");
        CHECK_EQ(r.countDisposition(Disposition::Waived), 0u); CHECK_EQ(count(r, "CST-022"), 1);
    }
}

TEST(template_rejects_either_placeholder_independently) {
    for (bool owner : {false, true}) {
        auto r = base(); r.finalize(); auto set = parse(waiverTemplate(r).dump().c_str());
        if (owner) set.mutableWaivers()[0].owner = "team";
        else set.mutableWaivers()[0].reason = "protocol reviewed";
        applyWaivers(r, set, "2026-11-01");
        CHECK_EQ(r.countDisposition(Disposition::Waived), 0u); CHECK_EQ(count(r, "CST-006"), 1);
    }
}
TEST(template_includes_unproved_findings_without_changing_proof) {
    auto r = base(); r.mutableDiagnostics()[0].proofState = ProofState::NotAttempted;
    r.mutableDiagnostics()[0].confidence = Confidence::Unresolved; r.finalize();
    auto t = waiverTemplate(r); CHECK(t["note"].asString().find("does not prove") != std::string::npos);
    auto set = parse(t.dump().c_str()); set.mutableWaivers()[0].owner = "team"; set.mutableWaivers()[0].reason = "accepted obligation";
    applyWaivers(r, set, "2026-11-01");
    CHECK_EQ(r.diagnostics()[0].proofState, ProofState::NotAttempted);
    CHECK_EQ(r.diagnostics()[0].confidence, Confidence::Unresolved);
}

namespace {
std::map<std::string, std::string> fingerprintsWith(const std::string& sdcText) {
    auto dir = std::filesystem::temp_directory_path() / "sverity_clock_identity";
    std::filesystem::create_directories(dir);
    auto sv = dir / "t.sv", sdc = dir / "t.sdc";
    { std::ofstream(sv) << "module top(input logic a, b, ra, rb, d, output logic q, p);\n  logic s;\n"
        "  always_ff @(posedge a or negedge ra) if (!ra) s <= 0; else s <= d;\n"
        "  always_ff @(posedge a or negedge rb) if (!rb) q <= 0; else q <= s;\n"
        "  always_ff @(posedge b) p <= d;\nendmodule\n"; }
    { std::ofstream(sdc) << sdcText; }
    AnalyzerOptions o;
    o.frontend.slangArgs = {"--top", "top", sv.string()};
    o.sdcFiles = {sdc.string()};
    o.pathRoot = dir.string();
    o.today = "2026-11-01";
    std::map<std::string, std::string> result;
    Report r = analyze(o);
    for (const auto& d : r.diagnostics()) result[d.ruleId + "/" + d.message.substr(0, 40)] = d.fingerprint;
    std::filesystem::remove_all(dir);
    return result;
}
} // namespace

TEST(clock_facts_are_identity_for_input_and_reset_findings) {
    const std::string clocks = "create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\n";
    auto declared = fingerprintsWith(clocks + "set_input_delay -clock a 1 [get_ports d]\n");
    auto changed = fingerprintsWith(clocks + "set_input_delay -clock b 1 [get_ports d]\n");
    int inputs = 0;
    for (const auto& [key, fingerprint] : declared)
        if (key.rfind("CDC-029/", 0) == 0) {
            inputs++;
            CHECK(changed.count(key) && changed[key] != fingerprint);
        }
    CHECK(inputs > 0);
    auto renamed = fingerprintsWith("create_clock -name other -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\n");
    auto original = fingerprintsWith(clocks);
    int resets = 0;
    for (const auto& [key, fingerprint] : original)
        if (key.rfind("RDC-003/", 0) == 0) {
            resets++;
            CHECK(renamed.count(key) && renamed[key] != fingerprint);
        }
    CHECK(resets > 0);
}

static const Diagnostic* findingOf(const Report& r, const char* rule) {
    for (const auto& d : r.diagnostics()) if (d.ruleId == rule) return &d;
    return nullptr;
}

TEST(waiver_of_one_rule_does_not_accept_the_others_of_a_primary_finding) {
    auto one = sourceReport("source_primary_one_rule");
    auto* open = findingOf(one, "CDC-028");
    CHECK(open && open->disposition != Disposition::Waived && open->severity == Severity::Error);
    CHECK(open && open->nextStep.find("CDC-003") != std::string::npos);
    CHECK(one.reviewStatus() == ReviewStatus::Fail);
    auto both = sourceReport("source_primary_every_rule");
    auto* waived = findingOf(both, "CDC-028");
    CHECK(waived && waived->disposition == Disposition::Waived && waived->severity == Severity::Error);
}

TEST(signal_waiver_covers_all_grouped_sources_or_nothing) {
    auto one = sourceReport("source_group_one_source");
    auto* open = findingOf(one, "RDC-004");
    CHECK(open && open->disposition != Disposition::Waived);
    auto destination = sourceReport("source_group_destination");
    auto* waived = findingOf(destination, "RDC-004");
    CHECK(waived && waived->disposition == Disposition::Waived);
}

TEST(severity_override_of_one_rule_does_not_lower_a_primary_finding) {
    const std::string root = SVERITY_SOURCE_DIR;
    auto run = [&](std::map<std::string, Severity> overrides) {
        AnalyzerOptions o;
        o.pathRoot = root;
        o.frontend.slangArgs = {"--top", "top", root + "/tests/fixtures/CDC-028/bad_primary_disabled_keeps_crossing.sv"};
        o.sdcFiles = {root + "/tests/fixtures/_common/async.sdc"};
        o.today = "2026-11-01";
        o.config.severityOverrides = std::move(overrides);
        return analyze(o);
    };
    auto one = run({{"CDC-028", Severity::Warning}});
    auto* kept = findingOf(one, "CDC-028");
    CHECK(kept && kept->severity == Severity::Error);
    auto both = run({{"CDC-028", Severity::Warning}, {"CDC-003", Severity::Warning}});
    auto* lowered = findingOf(both, "CDC-028");
    CHECK(lowered && lowered->severity == Severity::Warning);
}

TEST(inverting_a_capture_guard_stales_a_fingerprint) {
    auto fingerprint = [](const std::string& guard) {
        auto dir = std::filesystem::temp_directory_path() / "sverity_guard_identity";
        std::filesystem::create_directories(dir);
        auto sv = dir / "t.sv", sdc = dir / "t.sdc";
        { std::ofstream(sv) << "module top(input logic a, b, d, en, output logic q);\n  logic s;\n"
            "  always_ff @(posedge a) s <= d;\n  always_ff @(posedge b) if (" + guard + ") q <= s;\nendmodule\n"; }
        { std::ofstream(sdc) << "create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\n"
            "set_clock_groups -asynchronous -group {a} -group {b}\n"; }
        AnalyzerOptions o;
        o.frontend.slangArgs = {"--top", "top", sv.string()};
        o.sdcFiles = {sdc.string()};
        o.pathRoot = dir.string();
        o.today = "2026-11-01";
        Report r = analyze(o);
        std::string found;
        for (const auto& d : r.diagnostics()) if (d.ruleId == "CDC-028") found = d.fingerprint;
        std::filesystem::remove_all(dir);
        return found;
    };
    const auto high = fingerprint("en"), low = fingerprint("!en");
    CHECK(!high.empty() && !low.empty());
    CHECK(high != low);
}

TEST(inverting_the_guard_of_another_bit_stales_a_fingerprint) {
    auto fingerprint = [](const std::string& guard) {
        auto dir = std::filesystem::temp_directory_path() / "sverity_bit_guard_identity";
        std::filesystem::create_directories(dir);
        auto sv = dir / "t.sv", sdc = dir / "t.sdc";
        { std::ofstream(sv) << "module top(input logic a, b, input logic [1:0] d, input logic e0, e1, output logic [1:0] q);\n"
            "  logic [1:0] s;\n  always_ff @(posedge a) s <= d;\n"
            "  always_ff @(posedge b) begin if (e0) q[0] <= s[0]; if (" + guard + ") q[1] <= s[1]; end\nendmodule\n"; }
        { std::ofstream(sdc) << "create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\n"
            "set_clock_groups -asynchronous -group {a} -group {b}\n"; }
        AnalyzerOptions o;
        o.frontend.slangArgs = {"--top", "top", sv.string()};
        o.sdcFiles = {sdc.string()};
        o.pathRoot = dir.string();
        o.today = "2026-11-01";
        Report r = analyze(o);
        std::string found;
        for (const auto& d : r.diagnostics()) if (d.ruleId == "CDC-028") found = d.fingerprint;
        std::filesystem::remove_all(dir);
        return found;
    };
    const auto high = fingerprint("e1"), low = fingerprint("!e1");
    CHECK(!high.empty() && !low.empty());
    CHECK(high != low);
}
