#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "sverity/analyzer.h"
#include "testing.h"
using namespace sverity;

namespace {
Report qualifierCase(const std::string& declarations, const std::string& predicate) {
    static unsigned serial = 0;
    auto dir = std::filesystem::temp_directory_path() / ("sverity_qualifier_" + std::to_string(serial++));
    std::filesystem::create_directories(dir);
    auto file = dir / "input.sv";
    std::ofstream(file) << "module top(input logic clka, clkb, rst_n, load, value, local_ready, bypass, output logic result);\n"
        "logic payload, request, s1, s2;\n" << declarations << "\n"
        "always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin payload <= 0; request <= 0; end "
        "else begin payload <= value; request <= load; end\n"
        "always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin s1 <= 0; s2 <= 0; result <= 0; end "
        "else begin s1 <= request; s2 <= s1; if (" << predicate << ") result <= payload; end\nendmodule\n";
    AnalyzerOptions options;
    options.frontend.slangArgs = {"--top", "top", file.string()};
    options.sdcFiles = {std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/_common/async.sdc"};
    auto report = analyze(options);
    std::filesystem::remove_all(dir);
    return report;
}
const Diagnostic* payloadDiagnostic(const Report& report, const char* id) {
    for (const auto& d : report.diagnostics())
        if (d.ruleId == id && std::find(d.signals.begin(), d.signals.end(), "top.payload") != d.signals.end()) return &d;
    return nullptr;
}
}

TEST(qualifier_boolean_necessity_preserves_polarity_and_local_restrictions) {
    for (const char* predicate : {"s2 && local_ready", "!(!s2 || !local_ready)", "!s2 && local_ready"}) {
        auto report = qualifierCase("", predicate);
        auto* d = payloadDiagnostic(report, "CDC-039");
        CHECK(d && d->severity == Severity::Warning && d->confidence != Confidence::Unresolved);
        CHECK(payloadDiagnostic(report, "CDC-003") == nullptr);
    }
    auto report = qualifierCase("wire enabled = (s2 || bypass) && local_ready;", "enabled");
    auto* defect = payloadDiagnostic(report, "CDC-028");   // the primary finding; CDC-003 is its evidence
    CHECK(defect && defect->severity == Severity::Error);
    CHECK(payloadDiagnostic(report, "CDC-003") == nullptr);
    CHECK(payloadDiagnostic(report, "CDC-039") == nullptr);
}

TEST(qualifier_unknown_and_limits_are_visible_obligations) {
    const std::string opaque = "function automatic logic opaque_control(logic x, y); return x ^ y; endfunction";
    std::string longCone = "wire n0 = s2;\n";
    for (unsigned i = 1; i <= 70; ++i)
        longCone += "wire n" + std::to_string(i) + " = n" + std::to_string(i - 1) + ";\n";
    const std::string held = "logic held; always_ff @(posedge clkb or negedge rst_n) if (!rst_n) held <= 0; else if (s2) held <= 1;";
    for (const auto& input : {std::pair<std::string, std::string>{opaque, "opaque_control(s2, local_ready)"},
                              {"wire enabled = s2 ^ local_ready;", "enabled"}, {held, "s2 || held"}, {longCone, "n70"}}) {
        auto report = qualifierCase(input.first, input.second);
        auto* d = payloadDiagnostic(report, "CDC-039");
        CHECK(d && d->severity == Severity::Warning && d->confidence == Confidence::Unresolved);
        if (d) {
            CHECK_EQ(d->proofState, ProofState::NotAttempted);
            CHECK(d->message.find("qualifier pattern") == std::string::npos);
            CHECK(std::any_of(d->evidence.begin(), d->evidence.end(), [](const Evidence& e) { return e.kind == "proof_obligation"; }));
        }
        CHECK(payloadDiagnostic(report, "CDC-003") == nullptr);
        CHECK(payloadDiagnostic(report, "CDC-028") == nullptr);
        bool incomplete = false;
        for (const auto& scope : report.scopes())
            if (scope.scope == "payload-qualification" && scope.coverage == AnalysisCoverage::Incomplete) incomplete = true;
        CHECK(incomplete);
    }
}
