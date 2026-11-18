// sverity-args: --top top
// The index {1'b1, i} of a two-bit i takes 4 to 7, which the write to bit 0 never reaches.
// expect-none: LNT-009
module top(input logic clk, input logic [1:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[{1'b1, i}] <= a;
  always @(posedge clk) y[0] <= b;
endmodule
