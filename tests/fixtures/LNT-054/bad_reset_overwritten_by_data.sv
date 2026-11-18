// sverity-args: --top top
// The last write of the reset branch wins: the constant is replaced by the data, so no reset value is established.
// expect: LNT-054 WARNING 7
module top(input logic clk, rst, d, output logic y);
  logic q;
  always @(posedge clk) if (rst) begin q <= 0; q <= d; end else q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
// revert: q <= 0; q <= d; => q <= d; q <= 0;
// revert-expect-none: LNT-054
