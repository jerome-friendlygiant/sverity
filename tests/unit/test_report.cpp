#include "sverity/report.h"
#include "testing.h"
using namespace sverity;

static Diagnostic mk(const char* rule, Severity s, const char* msg, const char* file = "a.sv", uint32_t line = 1,
                     const char* hier = "top") {
    Diagnostic d;
    d.ruleId = rule; d.severity = s; d.message = msg; d.location = {file, line, 1}; d.hierarchy = hier;
    return d;
}
static Report fullCoverage() {
    Report r;
    r.addRuleCoverage({"LNT-001", RuleState::Executed, "implemented", "", 1, 0});
    return r;
}

TEST(dimensions_are_orthogonal) {
    Report r = fullCoverage();
    r.add(mk("LNT-001", Severity::Error, "boom"));
    r.markIncomplete("clock-extraction", "unknown generated clock");
    r.finalize();
    CHECK_EQ(r.executionStatus(), ExecutionStatus::Success);
    CHECK_EQ(r.analysisCoverage(), AnalysisCoverage::Incomplete);
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Fail);
    CHECK_EQ(r.exitCode(true), 1 | 4);
}
TEST(clean_complete_run_is_pass_exit0) {
    Report r = fullCoverage();
    r.add(mk("LNT-001", Severity::Info, "fyi"));
    r.finalize();
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Pass);
    CHECK_EQ(r.analysisCoverage(), AnalysisCoverage::Complete);
    CHECK_EQ(r.exitCode(true), 0);
}
TEST(warning_requires_review_and_unknown_is_not_safe) {
    Report r = fullCoverage();
    r.add(mk("LNT-001", Severity::Warning, "cannot tell"));
    r.finalize();
    CHECK_EQ(r.reviewStatus(), ReviewStatus::ReviewRequired);
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::ReviewRequired);
}
TEST(unimplemented_rule_makes_coverage_incomplete) {
    Report r;
    r.addRuleCoverage({"CDC-003", RuleState::Unimplemented, "proposed", "x", 0, 0});
    r.finalize();
    CHECK_EQ(r.analysisCoverage(), AnalysisCoverage::Incomplete);
}
TEST(fatal_sets_execution_error) {
    Report r = fullCoverage();
    r.add(mk("LNT-001", Severity::Fatal, "dead"));
    r.finalize();
    CHECK_EQ(r.executionStatus(), ExecutionStatus::Error);
    CHECK((r.exitCode(false) & 8) != 0);
}
TEST(duplicates_are_grouped_not_dropped) {
    Report r = fullCoverage();
    r.add(mk("LNT-011", Severity::Warning, "unused", "a.sv", 3, "top.u0"));
    r.add(mk("LNT-011", Severity::Warning, "unused", "a.sv", 3, "top.u1"));
    r.add(mk("LNT-011", Severity::Warning, "unused", "a.sv", 4, "top.u1"));
    r.finalize();
    CHECK_EQ(r.diagnostics().size(), 2u);
    CHECK_EQ(r.countSeverity(Severity::Warning), 3u);   // counts preserve every occurrence
    CHECK_EQ(r.diagnostics()[0].occurrences.size(), 2u);
}
TEST(finalize_is_idempotent_and_deterministic) {
    Report a = fullCoverage(), b = fullCoverage();
    a.add(mk("LNT-011", Severity::Warning, "x", "b.sv", 2)); a.add(mk("LNT-004", Severity::Warning, "y", "a.sv", 9));
    b.add(mk("LNT-004", Severity::Warning, "y", "a.sv", 9)); b.add(mk("LNT-011", Severity::Warning, "x", "b.sv", 2));
    a.finalize(); b.finalize(); a.finalize();
    CHECK_EQ(a.toJson().dump(), b.toJson().dump());
}
TEST(paths_are_made_relative) {
    Report r = fullCoverage();
    r.setPathRoot("/work/proj");
    r.add(mk("LNT-011", Severity::Warning, "x", "/work/proj/rtl/a.sv", 1));
    r.finalize();
    CHECK_EQ(r.diagnostics()[0].location.file, std::string("rtl/a.sv"));
}
TEST(console_truncation_is_announced) {
    Report r = fullCoverage();
    for (uint32_t i = 1; i <= 5; i++) r.add(mk("LNT-011", Severity::Warning, "x", "a.sv", i));
    r.finalize();
    CHECK(r.toText(false, 2).find("truncated") != std::string::npos);
    CHECK(r.toText(false, 0).find("truncated") == std::string::npos);
    CHECK_EQ(r.toJson()["diagnostics"].asArray().size(), 5u);   // JSON stays complete
}
TEST(sarif_has_all_results_and_levels) {
    Report r = fullCoverage();
    r.add(mk("LNT-001", Severity::Info, "i")); r.add(mk("LNT-001", Severity::Warning, "w", "a.sv", 2));
    r.add(mk("LNT-001", Severity::Error, "e", "a.sv", 3)); r.add(mk("LNT-001", Severity::Fatal, "f", "a.sv", 4));
    r.finalize();
    auto s = r.toSarif();
    CHECK_EQ(s["version"].asString(), std::string("2.1.0"));
    CHECK_EQ(s["runs"].asArray()[0]["results"].asArray().size(), 4u);
}

TEST(grouping_requires_identical_evidence) {
    Report r = fullCoverage();
    auto a = mk("LNT-011", Severity::Warning, "same", "a.sv", 3, "top.u0");
    a.evidence.push_back({"k", "first", {}});
    auto b = mk("LNT-011", Severity::Warning, "same", "a.sv", 3, "top.u1");
    b.evidence.push_back({"k", "second", {}});
    r.add(a); r.add(b);
    r.finalize();
    CHECK_EQ(r.diagnostics().size(), 2u);   // different evidence -> never merged (nothing is discarded)
}
TEST(empty_hierarchy_has_no_empty_occurrence) {
    Report r = fullCoverage();
    r.add(mk("LNT-011", Severity::Warning, "x", "a.sv", 3, ""));
    r.finalize();
    CHECK(r.diagnostics()[0].occurrences.empty());
}

// RFC section 3: an unproved property is at least a WARNING; INFO + NOT_ATTEMPTED needs a stated trust model.
TEST(unproved_obligation_is_never_info) {
    Report r = fullCoverage();
    Diagnostic bare = mk("CDC-024", Severity::Info, "obligation");
    bare.proofState = ProofState::NotAttempted;
    Diagnostic trusted = mk("CDC-003", Severity::Info, "recognised synchroniser", "a.sv", 2);
    trusted.proofState = ProofState::NotAttempted;
    trusted.evidence.push_back({"trust_model", "2FF assumed", {}});
    r.add(bare);
    r.add(trusted);
    r.finalize();
    CHECK(r.diagnostics()[0].severity == Severity::Warning);
    CHECK(r.diagnostics()[1].severity == Severity::Info);
}
