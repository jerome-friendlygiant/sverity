// sverity-args: --top top
// The reset call sits under repeat (0): it never runs, so reset establishes nothing.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, d, output logic y);
  logic q;
  task clear; q <= 0; endtask
  always @(posedge clk) if (rst) repeat (0) clear(); else q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
// revert: repeat (0) clear() => clear()
// revert-expect-none: LNT-054
