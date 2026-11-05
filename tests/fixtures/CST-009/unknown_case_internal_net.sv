// sverity-args: --top top --sdc @DIR@/unknown_case_internal_net.sdc
// expect: CST-009 WARNING
// expect-none: CST-018
// expect: CDC-028 ERROR
// expect-coverage: constraints INCOMPLETE
// revert: if (mode) => if (1'b0)
// revert-expect-none: CDC-028 ERROR
module top(input logic clk_a, clk_b, select_mode, output logic q);
  wire mode = select_mode;
  logic source_q;
  always_ff @(posedge clk_a) source_q <= ~source_q;
  always_ff @(posedge clk_b) if (mode) q <= source_q;
endmodule
