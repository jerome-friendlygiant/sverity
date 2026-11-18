// sverity-args: --top top
// sverity-args: --profile asic
// The initial block gives q an initial value; that an ASIC flow ignores it is LNT-048's finding, not LNT-054's.
// expect-none: LNT-054
// expect: LNT-048 WARNING 8
module top(input logic clk, d, output logic y);
  logic q;
  initial begin q = 0; end
  always @(posedge clk) q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
