// sverity-args: --top top
// expect: LNT-048 WARNING
// expect-unresolved: LNT-048
// expect-none: LNT-054
module top(input logic clk, d, output logic q = 0, output logic y);
  always_ff @(posedge clk) q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
