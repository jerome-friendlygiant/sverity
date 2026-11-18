// sverity-args: --top top
// $signed(i) >>> 1 of a three-bit i takes -2 to 1, so ($signed(i) >>> 1) + 2 takes 0 to 3 and never bit 4.
// expect-none: LNT-009
module top(input logic clk, input logic [2:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[($signed(i) >>> 1) + 2] <= a;
  always @(posedge clk) y[4] <= b;
endmodule
