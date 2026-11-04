// sverity-args: --top top --sdc @DIR@/unknown_case_mux_mode.sdc
// expect: CST-014 WARNING
// expect-none: CDC-019
// expect-none: CDC-003
// expect-coverage: constraints INCOMPLETE
// revert: sel ? clk_b : clk_a => clk_a
// revert-expect-none: CDC-019
module top(input logic clk_a, clk_b, sel, output logic q);
  logic source_q;
  wire selected_clk = sel ? clk_b : clk_a;
  always_ff @(posedge clk_a) source_q <= ~source_q;
  always_ff @(posedge selected_clk) q <= source_q;
endmodule
