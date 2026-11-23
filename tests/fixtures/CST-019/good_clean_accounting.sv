// sverity-args: --top top
// Good: no unknown disabled rules and all rules execute cleanly.
// expect-none: CST-019
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
