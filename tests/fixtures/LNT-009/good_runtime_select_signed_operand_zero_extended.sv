// sverity-args: --top top
// A signed two-bit i in an unsigned three-bit sum is zero-extended: the select reaches bits 2 to 5 and never bit 0.
// expect-none: LNT-009
module top(input logic clk, input logic signed [1:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[i + 3'd2] <= a;
  always @(posedge clk) y[0] <= b;
endmodule
