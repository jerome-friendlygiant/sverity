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
const Diagnostic* payloadDiagnostic(const Report& report, const char* id, const char* signal = "top.payload") {
    for (const auto& d : report.diagnostics())
        if (d.ruleId == id && std::find(d.signals.begin(), d.signals.end(), signal) != d.signals.end()) return &d;
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

TEST(qualifier_scalar_comparison_distinguishes_raw_request_from_synchronized_enable) {
    auto raw = qualifierCase("logic previous; always_ff @(posedge clkb) previous <= s2;",
                             "request != previous && local_ready");
    auto* defect = payloadDiagnostic(raw, "CDC-028");
    CHECK(defect && defect->severity == Severity::Error && defect->confidence == Confidence::Established);

    auto synchronized = qualifierCase("", "(s2 == 1'b1) && local_ready");
    auto* obligation = payloadDiagnostic(synchronized, "CDC-039");
    CHECK(obligation && obligation->confidence == Confidence::Heuristic);
    CHECK(payloadDiagnostic(synchronized, "CDC-028") == nullptr);
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

TEST(primary_input_payload_obligation_names_qualified_capture) {
    auto dir = std::filesystem::temp_directory_path() / "sverity_primary_input_qualified_endpoint";
    std::filesystem::create_directories(dir);
    auto file = dir / "input.sv";
    std::ofstream(file) << "module top(input logic src_clk, dst_clk, req, data, output logic q, z);\n"
        "logic r1, r2;\n"
        "always_ff @(posedge dst_clk) begin r1 <= req; r2 <= r1; end\n"
        "always_ff @(posedge dst_clk) q <= data;\n"
        "always_ff @(posedge dst_clk) if (r2) z <= data;\n"
        "endmodule\n";
    AnalyzerOptions options;
    options.frontend.slangArgs = {"--top", "top", file.string()};
    options.sdcFiles = {std::string(SVERITY_SOURCE_DIR) + "/tests/fixtures/CDC-029/input_async.sdc"};
    auto report = analyze(options);
    std::filesystem::remove_all(dir);
    auto it = std::find_if(report.diagnostics().begin(), report.diagnostics().end(), [](const Diagnostic& d) {
        return d.ruleId == "CDC-039";
    });
    CHECK(it != report.diagnostics().end());
    if (it != report.diagnostics().end() && it->crossing) {
        CHECK_EQ(it->crossing->capture.signal, "top.z");
        CHECK_EQ(it->crossing->widthBits, 1u);
        CHECK_EQ(it->location.line, 5u);
    }
}

namespace {
Report analyzeInline(const std::string& body, const std::string& sdcRel = "/tests/fixtures/_common/async.sdc") {
    static unsigned serial = 1000;
    auto dir = std::filesystem::temp_directory_path() / ("sverity_qualifier_ext_" + std::to_string(serial++));
    std::filesystem::create_directories(dir);
    auto file = dir / "input.sv";
    std::ofstream(file) << body;
    AnalyzerOptions options;
    options.frontend.slangArgs = {"--top", "top", file.string()};
    options.sdcFiles = {std::string(SVERITY_SOURCE_DIR) + sdcRel};
    auto report = analyze(options);
    std::filesystem::remove_all(dir);
    return report;
}
}

TEST(qualifier_non_hold_mux_arms_distinguish_local_from_raw_cross_domain) {
    // Safe: ternary non-qualifying arms with local flop, processed feedback, or constant.
    auto safe = analyzeInline(
        "module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, local_in, output logic [3:0] q, r, z);\n"
        "  logic [3:0] data, local_q;\n"
        "  logic v_a, v1, v2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) begin {v2, v1} <= '0; local_q <= '0; end else begin {v2, v1} <= {v1, v_a}; local_q <= local_in; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) begin q <= '0; r <= '0; z <= '0; end\n"
        "    else begin\n"
        "      q <= v2 ? data : local_q;\n"
        "      r <= v2 ? data : (r + 4'd1);\n"
        "      z <= v2 ? data : '0;\n"
        "    end\n"
        "endmodule\n");
    CHECK(payloadDiagnostic(safe, "CDC-028", "top.data") == nullptr);
    CHECK(payloadDiagnostic(safe, "CDC-016", "top.data") == nullptr);
    auto* q39 = payloadDiagnostic(safe, "CDC-039", "top.data");
    CHECK(q39 && q39->severity == Severity::Warning && q39->confidence == Confidence::Heuristic);

    // Unsafe lookalike: non-qualifying arm reads another raw launch flop from clka.
    auto unsafeAlt = analyzeInline(
        "module top(input logic clka, clkb, rst_n, load, input logic [3:0] d1, d2, output logic [3:0] q);\n"
        "  logic [3:0] data, other_data;\n"
        "  logic v_a, v1, v2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; other_data <= '0; v_a <= 1'b0; end else begin data <= d1; other_data <= d2; v_a <= load; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) q <= '0; else q <= v2 ? data : other_data;\n"
        "endmodule\n");
    auto* d28 = payloadDiagnostic(unsafeAlt, "CDC-028", "top.data");
    CHECK(d28 && d28->severity == Severity::Error && d28->confidence == Confidence::Established);
    CHECK(payloadDiagnostic(unsafeAlt, "CDC-039", "top.data") == nullptr);
}

TEST(qualifier_staged_always_comb_next_state_safe_and_unsafe_lookalikes) {
    // Safe: always_comb with hold default, local default, and ternary alias.
    auto safe = analyzeInline(
        "module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, local_in, output logic [3:0] q, r, t);\n"
        "  logic [3:0] data, q_d, r_d, t_d, local_q;\n"
        "  logic v_a, v1, v2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) begin {v2, v1} <= '0; local_q <= '0; end else begin {v2, v1} <= {v1, v_a}; local_q <= local_in; end\n"
        "  always_comb begin q_d = q; if (v2) q_d = data; end\n"
        "  always_comb begin r_d = local_q; if (v2) r_d = data; end\n"
        "  assign t_d = v2 ? data : t;\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) begin q <= '0; r <= '0; t <= '0; end else begin q <= q_d; r <= r_d; t <= t_d; end\n"
        "endmodule\n");
    CHECK(payloadDiagnostic(safe, "CDC-028", "top.data") == nullptr);
    CHECK(payloadDiagnostic(safe, "CDC-016", "top.data") == nullptr);
    auto* q39 = payloadDiagnostic(safe, "CDC-039", "top.data");
    CHECK(q39 && q39->severity == Severity::Warning && q39->confidence == Confidence::Heuristic);

    // Unsafe lookalike 1: data_d defaults to raw cross-domain data.
    auto defaultBypass = analyzeInline(
        "module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);\n"
        "  logic [3:0] data, data_d;\n"
        "  logic v_a, v1, v2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};\n"
        "  always_comb begin data_d = data; if (v2) data_d = data; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) q <= '0; else q <= data_d;\n"
        "endmodule\n");
    auto* bypassErr = payloadDiagnostic(defaultBypass, "CDC-028", "top.data");
    CHECK(bypassErr && bypassErr->severity == Severity::Error && bypassErr->confidence == Confidence::Established);
    CHECK(payloadDiagnostic(defaultBypass, "CDC-039", "top.data") == nullptr);

    // Unsafe lookalike 2: data_d overwritten unconditionally after if (v2).
    auto overwriteBypass = analyzeInline(
        "module top(input logic clka, clkb, rst_n, load, input logic [3:0] d1, d2, output logic [3:0] q);\n"
        "  logic [3:0] data, other_data, data_d;\n"
        "  logic v_a, v1, v2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; other_data <= '0; v_a <= 1'b0; end else begin data <= d1; other_data <= d2; v_a <= load; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};\n"
        "  always_comb begin data_d = q; if (v2) data_d = data; data_d = other_data; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) q <= '0; else q <= data_d;\n"
        "endmodule\n");
    bool sawError = false;
    for (const auto& d : overwriteBypass.diagnostics())
        if ((d.ruleId == "CDC-016" || d.ruleId == "CDC-028" || d.ruleId == "CDC-004") && d.severity == Severity::Error) sawError = true;
    CHECK(sawError);
    CHECK(payloadDiagnostic(overwriteBypass, "CDC-039", "top.data") == nullptr);
}

TEST(qualifier_bundle_coherence_flags_distinct_and_opposite_qualifiers) {
    auto distinct = analyzeInline(
        "module top(input logic clka, clkb, rst_n, l1, l2, input logic [3:0] d, output logic [3:0] q);\n"
        "  logic [3:0] data;\n"
        "  logic a1, b1, v1, v2, a2, w1, w2;\n"
        "  always_ff @(posedge clka or negedge rst_n)\n"
        "    if (!rst_n) begin data <= '0; a1 <= 0; a2 <= 0; end else begin data <= d; a1 <= l1; a2 <= l2; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) begin {v2, v1} <= '0; {w2, w1} <= '0; end else begin {v2, v1} <= {v1, a1}; {w2, w1} <= {w1, a2}; end\n"
        "  always_ff @(posedge clkb or negedge rst_n)\n"
        "    if (!rst_n) q <= '0; else begin if (v2) q[1:0] <= data[1:0]; if (w2) q[3:2] <= data[3:2]; end\n"
        "endmodule\n");
    auto* d39 = payloadDiagnostic(distinct, "CDC-039", "top.data");
    CHECK(d39 && d39->severity == Severity::Warning && d39->confidence == Confidence::Unresolved);

    // Primary-input multi-bit bundle coherence: opposite polarities across bits.
    auto piOpposite = analyzeInline(
        "module top(input logic src_clk, dst_clk, req, input logic [3:0] data, output logic [3:0] q);\n"
        "  logic r1, r2;\n"
        "  always_ff @(posedge dst_clk) begin r1 <= req; r2 <= r1; end\n"
        "  always_ff @(posedge dst_clk) begin\n"
        "    if (r2) q[1:0] <= data[1:0];\n"
        "    if (!r2) q[3:2] <= data[3:2];\n"
        "  end\n"
        "endmodule\n",
        "/tests/fixtures/CDC-029/input_async.sdc");
    bool sawUnresolved39 = false, sawHeuristic39 = false;
    for (const auto& d : piOpposite.diagnostics()) {
        if (d.ruleId != "CDC-039") continue;
        if (d.confidence == Confidence::Unresolved) sawUnresolved39 = true;
        if (d.confidence == Confidence::Heuristic) sawHeuristic39 = true;
    }
    CHECK(sawUnresolved39);
    CHECK(!sawHeuristic39);
}
