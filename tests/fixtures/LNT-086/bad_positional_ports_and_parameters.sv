// sverity-args: --top top
// Positional parameter override and positional port list; the width of each connection matches.
// expect: LNT-086 INFO 9
// expect-count: LNT-086 INFO 2
module sub #(parameter W = 4, parameter D = 2) (input logic clk, input logic [W-1:0] a, output logic [W-1:0] y);
  always_ff @(posedge clk) y <= a;
endmodule
module top(input logic clk, input logic [7:0] p, output logic [7:0] r);
  sub #(8, 3) u0 (clk, p, r);
endmodule
