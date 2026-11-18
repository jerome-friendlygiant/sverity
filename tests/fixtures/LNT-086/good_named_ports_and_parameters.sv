// sverity-args: --top top
// Named connections and a named override; a gate primitive is positional by definition.
// expect-none: LNT-086
module sub #(parameter W = 4) (input logic clk, input logic [W-1:0] a, output logic [W-1:0] y);
  always_ff @(posedge clk) y <= a;
endmodule
module top(input logic clk, input logic [7:0] p, input logic e, output logic [7:0] r, output logic g);
  sub #(.W(8)) u0 (.clk(clk), .a(p), .y(r));
  and (g, e, clk);
endmodule
