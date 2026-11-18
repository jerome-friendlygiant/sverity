// sverity-args: --top top
// The same register on both edges is LNT-022's finding, not this one.
// expect: LNT-022 WARNING
// expect-none: LNT-085
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
  always_ff @(negedge clk) q <= ~d;
endmodule
