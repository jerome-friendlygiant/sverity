// sverity-args: --top top
// A defparam from outside the instance.
// expect: LNT-086 INFO 9
module sub #(parameter W = 4) (input logic clk, input logic [W-1:0] a, output logic [W-1:0] y);
  always_ff @(posedge clk) y <= a;
endmodule
module top(input logic clk, input logic [7:0] p, output logic [7:0] r);
  sub u1 (.clk(clk), .a(p), .y(r));
  defparam u1.W = 8;
endmodule
