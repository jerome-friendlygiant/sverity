#include <algorithm>
#include <string>

#include "sverity/analyzer.h"
#include "testing.h"
using namespace sverity;

TEST(cdc017_opaque_function_is_unresolved_with_obligation) {
    const std::string root = SVERITY_SOURCE_DIR;
    for (const char* name : {"unknown_misnamed_binary_function.sv", "unknown_function_loop.sv"}) {
        AnalyzerOptions opts;
        opts.frontend.slangArgs = {"--top", "top", root + "/tests/fixtures/CDC-017/" + name};
        opts.sdcFiles = {root + "/tests/fixtures/_common/async.sdc"};
        auto report = analyze(opts);
        bool found = false, incomplete = false;
        for (const auto& d : report.diagnostics()) {
            if (d.ruleId != "CDC-017") continue;
            found = true;
            CHECK_EQ(d.severity, Severity::Warning);
            CHECK_EQ(d.confidence, Confidence::Unresolved);
            CHECK_EQ(d.proofState, ProofState::NotAttempted);
            CHECK(std::any_of(d.evidence.begin(), d.evidence.end(), [](const Evidence& e) { return e.kind == "proof_obligation"; }));
        }
        for (const auto& scope : report.scopes())
            if (scope.scope == "cdc-017" && scope.coverage == AnalysisCoverage::Incomplete) incomplete = true;
        CHECK(found);
        CHECK(incomplete);
    }
}

TEST(cdc017_split_register_partial_sync_gray_is_error) {
    const std::string root = SVERITY_SOURCE_DIR;
    AnalyzerOptions opts;
    opts.config.profile = "asic";
    opts.config.goals = {"cdc"};
    opts.config.deck = "cdc";
    opts.frontend.slangArgs = {"--top", "top", root + "/tests/fixtures/CDC-017/bad_gray_split_register_partial_sync.sv"};
    opts.sdcFiles = {root + "/tests/fixtures/_common/async.sdc"};
    auto report = analyze(opts);
    bool seen = false;
    for (const auto& d : report.diagnostics()) {
        if (d.ruleId != "CDC-017") continue;
        seen = true;
        CHECK_EQ(d.severity, Severity::Error);
        CHECK_EQ(d.confidence, Confidence::Established);
        CHECK(std::find(d.signals.begin(), d.signals.end(), "top.gray") != d.signals.end());
        CHECK(std::find(d.signals.begin(), d.signals.end(), "top.s2") != d.signals.end());
    }
    CHECK(seen);
}
