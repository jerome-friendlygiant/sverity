// The custom-rule extension point: embedders register rules; they run, are listed in coverage and cannot hide findings.
#include "sverity/lint.h"
#include "sverity/analyzer.h"
#include "testing.h"
using namespace sverity;

namespace {
// A user rule: flag every module named "top". One class, no other plumbing.
class NoTopRule final : public lint::LintRule {
public:
    NoTopRule() : LintRule("ACME-001", "demo rule: only checks module names") {}
    void check(lint::Lint& l) override {
        for (auto* top : l.compilation().getRoot().topInstances) {
            l.count();
            l.report(*top, "module '" + std::string(top->name) + "' is a top module");
        }
    }
};
} // namespace

TEST(custom_rule_runs_and_is_listed) {
    registerRule([] { return std::make_unique<NoTopRule>(); });
    AnalyzerOptions o;
    o.frontend.slangArgs = {"--top", "top", std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/LNT-004/good_same_width.sv"};
    o.today = "2026-11-01";
    Report r = analyze(o);
    clearRegisteredRules();
    bool found = false, listed = false;
    for (auto& d : r.diagnostics()) found = found || d.ruleId == "ACME-001";
    for (auto& rc : r.ruleCoverage())
        if (rc.ruleId == "ACME-001") listed = rc.state == RuleState::Partial && rc.catalogStatus == "custom";
    CHECK(found);
    CHECK(listed);
}
