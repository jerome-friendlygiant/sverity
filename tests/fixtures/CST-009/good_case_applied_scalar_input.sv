// sverity-args: --top top --sdc @DIR@/good_case_applied_scalar_input.sdc
// Near-miss of unknown_case_mux_mode: sel is a scalar top-level input, so set_case_analysis is applied and not disclosed.
// expect-none: CST-009
module top(input logic clk, sel, d, output logic q);
  always_ff @(posedge clk) q <= sel & d;
endmodule
