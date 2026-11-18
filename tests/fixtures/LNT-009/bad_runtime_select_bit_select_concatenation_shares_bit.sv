// sverity-args: --top top
// The index {i[1], i[1:0]} of a two-bit i takes 0, 1, 6 and 7, and the other process writes bit 6.
// expect: LNT-009 ERROR 6
module top(input logic clk, input logic [1:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[{i[1], i[1:0]}] <= a;
  always @(posedge clk) y[6] <= b;
endmodule
