// sverity-args: --top top
// `c2` is a plain copy of `clk`: `posedge clk` and `negedge c2` are one net on both edges of one register.
// expect: LNT-022 WARNING 6
module top(input logic clk, d, output logic q);
  wire c2 = clk;
  always_ff @(posedge clk or negedge c2) if (!c2) q <= 1'b0; else q <= d;
endmodule
