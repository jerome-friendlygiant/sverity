// sverity-args: --top top
// i >> 1 of a three-bit i takes 0 to 3, so the write to bit 5 is never reached.
// expect-none: LNT-009
module top(input logic clk, input logic [2:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[i >> 1] <= a;
  always @(posedge clk) y[5] <= b;
endmodule
