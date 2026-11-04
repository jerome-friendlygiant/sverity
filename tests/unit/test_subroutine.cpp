#include <algorithm>
#include <string>

#include "sverity/analyzer.h"
#include "testing.h"
using namespace sverity;

TEST(subroutine_unknowns_name_each_affected_capture_and_keep_obligations) {
    for (const char* fixture : {"unknown_subroutine_side_effects", "unknown_recursive_function"}) {
        AnalyzerOptions options;
        options.frontend.slangArgs = {"--top", "top", std::string(SVERITY_SOURCE_DIR) +
            "/tests/fixtures/CST-010/" + fixture + ".sv"};
        options.sdcFiles = {std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc"};
        auto report = analyze(options);
        const bool recursive = std::string(fixture) == "unknown_recursive_function";
        const std::vector<std::string> captures = recursive ? std::vector<std::string>{"top.result"} :
            std::vector<std::string>{"top.function_result", "top.side_result", "top.task_result"};
        for (const auto& capture : captures) {
            auto finding = std::find_if(report.diagnostics().begin(), report.diagnostics().end(), [&](const Diagnostic& d) {
                return d.ruleId == "CST-010" && d.confidence == Confidence::Unresolved &&
                    std::find(d.signals.begin(), d.signals.end(), capture) != d.signals.end();
            });
            CHECK(finding != report.diagnostics().end());
            if (finding == report.diagnostics().end()) continue;
            CHECK_EQ(finding->severity, Severity::Warning);
            CHECK_EQ(finding->proofState, ProofState::NotAttempted);
            CHECK(finding->message.find(capture) != std::string::npos);
            CHECK(finding->message.find(recursive ? "descend" : "transfer") != std::string::npos);
            CHECK(std::any_of(finding->evidence.begin(), finding->evidence.end(),
                [](const Evidence& e) { return e.kind == "proof_obligation"; }));
        }
        CHECK(std::any_of(report.scopes().begin(), report.scopes().end(), [](const ScopeCoverage& scope) {
            return scope.scope == "graph-precision" && scope.coverage == AnalysisCoverage::Incomplete;
        }));
    }
}
