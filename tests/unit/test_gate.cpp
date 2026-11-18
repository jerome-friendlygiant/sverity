// Which Unresolved findings are obligations or gaps, and what each exit policy does with them.
#include "sverity/report.h"
#include "testing.h"
using namespace sverity;

static Diagnostic unresolved(const char* rule, const char* obligation = "", Confidence c = Confidence::Unresolved,
                             ProofState p = ProofState::NotAttempted, uint32_t line = 1) {
    Diagnostic d;
    d.ruleId = rule; d.severity = Severity::Warning; d.message = "cannot decide"; d.location = {"a.sv", line, 1};
    d.confidence = c; d.proofState = p; d.obligation = obligation;
    return d;
}
static Report runWith(Diagnostic d) {
    Report r;
    r.addRuleCoverage({"LNT-082", RuleState::Executed, "implemented", "", 1, 0});
    r.add(std::move(d));
    r.finalize();
    return r;
}

TEST(gate_declared_obligation_is_counted_and_does_not_block) {
    auto r = runWith(unresolved("LNT-082", "memory-read-during-write"));
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::Obligation);
    CHECK_EQ(r.diagnostics()[0].unresolvedKind, UnresolvedKind::Obligation);
    CHECK_EQ(r.reviewStatus(), ReviewStatus::Pass);
    CHECK_EQ(r.exitCode(ExitPolicy::Gate), 0);
    CHECK_EQ(r.countDisposition(Disposition::Obligation), 1u);
    CHECK_EQ(r.countDefects(), 0u);
    CHECK_EQ(r.exitCode(ExitPolicy::Strict), 2);
}
TEST(gate_lint_unresolved_without_declaration_is_a_gap) {
    auto r = runWith(unresolved("LNT-082"));
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::NotAnalysed);
    CHECK_EQ(r.exitCode(ExitPolicy::Gate), 0);
    CHECK_EQ(r.exitCode(ExitPolicy::Strict), 2);
    // Unknown proof state at Established confidence is the same floor.
    auto p = runWith(unresolved("LNT-082", "", Confidence::Established, ProofState::Unknown));
    CHECK_EQ(p.diagnostics()[0].disposition, Disposition::NotAnalysed);
}
TEST(gate_an_undeclared_obligation_id_keeps_gating) {
    auto r = runWith(unresolved("LNT-082", "not-declared-by-this-rule"));
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::ReviewRequired);
    CHECK_EQ(r.exitCode(ExitPolicy::Gate), 2);
    auto other = runWith(unresolved("LNT-082", "fsm-reachability-liveness"));   // declared by LNT-071, not LNT-082
    CHECK_EQ(other.exitCode(ExitPolicy::Gate), 2);
}
TEST(gate_heuristic_established_cdc_rdc_and_cst_keep_gating) {
    CHECK_EQ(runWith(unresolved("LNT-082", "", Confidence::Heuristic, ProofState::NotAttempted)).exitCode(ExitPolicy::Gate), 2);
    CHECK_EQ(runWith(unresolved("LNT-013", "", Confidence::Established, ProofState::NotApplicable)).exitCode(ExitPolicy::Gate), 2);
    for (const char* rule : {"CDC-003", "RDC-003", "CST-011"}) {
        auto r = runWith(unresolved(rule));
        CHECK_EQ(r.diagnostics()[0].disposition, Disposition::ReviewRequired);
        CHECK_EQ(r.exitCode(ExitPolicy::Gate), 2);
    }
    auto unknownRule = runWith(unresolved("LNT-999"));   // no catalog entry, no declaration
    CHECK_EQ(unknownRule.exitCode(ExitPolicy::Gate), 2);
}
TEST(gate_a_waived_obligation_stays_waived_and_a_severity_floor_keeps_the_kind) {
    Diagnostic d = unresolved("LNT-082", "memory-read-during-write");
    d.severity = Severity::Info;   // Report::add raises an unproved INFO to WARNING; the kind follows
    auto r = runWith(d);
    CHECK_EQ(r.diagnostics()[0].severity, Severity::Warning);
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::Obligation);
}
TEST(gate_baseline_blocks_what_is_new) {
    auto r = runWith(unresolved("LNT-082", "memory-read-during-write"));
    const auto fingerprint = r.diagnostics()[0].fingerprint;
    r.setBaseline({fingerprint});
    CHECK_EQ(r.exitCode(ExitPolicy::Gate), 0);
    r.setBaseline({"0000000000000000"});
    CHECK_EQ(r.exitCode(ExitPolicy::Gate), 2);
    CHECK_EQ(r.exitCode(ExitPolicy::Violations), 0);
}
TEST(gate_json_and_text_always_count_unresolved) {
    auto r = runWith(unresolved("LNT-082", "memory-read-during-write"));
    r.add(unresolved("LNT-082", "", Confidence::Unresolved, ProofState::NotAttempted, 2));
    r.finalize();
    const auto j = r.toJson();
    CHECK_EQ(j["summary"]["unresolved"]["obligations"].asInt(), 1);
    CHECK_EQ(j["summary"]["unresolved"]["not_analysed"].asInt(), 1);
    CHECK_EQ(j["summary"]["disposition_counts"]["OBLIGATION"].asInt(), 1);
    CHECK_EQ(j["summary"]["tool_coverage"]["rules_total"].asInt(), 1);
    const auto text = r.toText(false);
    CHECK(text.find("PASS: 0 defects; 1 obligations, 1 not analysed (see report)\n") == text.size() - std::string("PASS: 0 defects; 1 obligations, 1 not analysed (see report)\n").size());
}
TEST(gate_unattributed_fsm_relation_is_no_longer_a_declared_obligation) {
    // A relation the inventory cannot attribute is a tool gap: only the liveness question of LNT-071 is declared.
    auto r = runWith(unresolved("LNT-029", "fsm-relation-unattributed"));
    CHECK_EQ(r.diagnostics()[0].disposition, Disposition::ReviewRequired);
    auto gap = runWith(unresolved("LNT-029"));
    CHECK_EQ(gap.diagnostics()[0].disposition, Disposition::NotAnalysed);
    CHECK_EQ(runWith(unresolved("LNT-071", "fsm-reachability-liveness")).diagnostics()[0].disposition, Disposition::Obligation);
}
TEST(gate_json_and_sarif_state_review_and_coverage_under_the_requested_policy) {
    auto r = runWith(unresolved("LNT-082", "memory-read-during-write"));
    CHECK_EQ(r.toJson()["summary"]["review_status"].asString(), std::string("PASS"));
    r.setExitPolicy(ExitPolicy::Strict);
    CHECK_EQ(r.toJson()["summary"]["review_status"].asString(), std::string("REVIEW_REQUIRED"));
    CHECK_EQ(r.toSarif()["runs"].asArray()[0]["properties"]["reviewStatus"].asString(), std::string("REVIEW_REQUIRED"));
    CHECK_EQ(r.toJson()["summary"]["review_status"].asString(), toString(r.reviewStatus(ExitPolicy::Strict)));
    CHECK_EQ(r.toJson()["summary"]["analysis_coverage"].asString(), toString(r.analysisCoverage(ExitPolicy::Strict)));
}
TEST(gate_defect_count_covers_everything_the_gate_blocks_on) {
    Report r;
    r.addRuleCoverage({"LNT-082", RuleState::Executed, "implemented", "", 1, 0});
    Diagnostic d = unresolved("LNT-013", "", Confidence::Established, ProofState::NotApplicable);
    d.severity = Severity::Info;                  // a severity override lowered it; the finding is still REVIEW_REQUIRED
    d.disposition = Disposition::ReviewRequired;
    r.add(std::move(d));
    r.finalize();
    CHECK_EQ(r.reviewStatus(), ReviewStatus::ReviewRequired);
    CHECK_EQ(r.countDefects(), 1u);               // the last line never says "0 defects" while blocking
}
TEST(gate_baseline_marks_new_unresolved_findings) {
    auto r = runWith(unresolved("LNT-082", "memory-read-during-write"));
    r.setBaseline({"0000000000000000"});
    const auto j = r.toJson();
    CHECK(j["diagnostics"].asArray()[0]["new_against_baseline"].asBool());
    CHECK_EQ(j["summary"]["unresolved"]["new_against_baseline"].asInt(), 1);
    CHECK(r.toText(false).find("NEW against baseline") != std::string::npos);
    r.setBaseline({r.diagnostics()[0].fingerprint});
    CHECK(!r.toJson()["diagnostics"].asArray()[0]["new_against_baseline"].asBool());
}
