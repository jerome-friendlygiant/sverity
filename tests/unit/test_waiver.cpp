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
    bool cdc3 = false, cdc28 = false;
    for (const auto& d : r.diagnostics()) {
        if (d.disposition != Disposition::Waived) continue;
        cdc3 |= d.ruleId == "CDC-003"; cdc28 |= d.ruleId == "CDC-028";
    }
    CHECK(cdc3); CHECK(cdc28); CHECK_EQ(count(r, "CST-022"), 1);
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
