// sverity-args: --top top
// An inversion of a two-input gate is logic on the clock, not a single inversion.
// expect: LNT-047 WARNING 6
module top(input logic clk, en, d, output logic q);
  logic gated_n;
  assign gated_n = ~(clk & en);
  always_ff @(posedge gated_n) q <= d;
endmodule
// revert: ~(clk & en) => ~clk
// revert-expect-none: LNT-047 WARNING
