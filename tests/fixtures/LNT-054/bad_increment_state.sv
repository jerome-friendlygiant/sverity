// sverity-args: --top top
// A counter that only increments never leaves its unknown start value, so it is a register without reset.
// expect: LNT-054 WARNING 7
module top(input logic clk, d, output logic y);
  integer q;
  always @(posedge clk) q++;
  always_comb if (q) y = d; else y = 0;
endmodule
