// sverity-args: --top top
// The select is an input, not an elaboration constant: a clock multiplexer.
// expect: LNT-047 WARNING 6
module top(input logic clk_a, clk_b, sel, d, output logic q);
  logic c;
  assign c = sel ? clk_a : clk_b;
  always_ff @(posedge c) q <= d;
endmodule
