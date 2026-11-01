#include "sverity/waiver.h"
#include "testing.h"
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
    CHECK_EQ(count(r, "CST-006"), 1);
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
