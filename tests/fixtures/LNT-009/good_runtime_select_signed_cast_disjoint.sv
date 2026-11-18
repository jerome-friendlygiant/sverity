// sverity-args: --top top
// $signed(i) of a two-bit i takes -2 to 1: the negative values select no bit, so the write to bits 3:2 is never reached.
// expect-none: LNT-009
module top(input logic clk, input logic [1:0] i, input logic a, b, output logic [3:0] y);
  always @(posedge clk) y[$signed(i)] <= a;
  always @(posedge clk) y[3:2] <= b;
endmodule
