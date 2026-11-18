// sverity-args: --top top
// i >> 1 of a three-bit i takes 0 to 3, which reaches the bit the other process writes.
// expect: LNT-009 ERROR 6
module top(input logic clk, input logic [2:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[i >> 1] <= a;
  always @(posedge clk) y[3] <= b;
endmodule
