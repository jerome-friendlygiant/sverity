// sverity-args: --top top
// complete level lists, implicit lists, always_comb, edge lists, element reads and block-local temporaries: no finding
// expect-none: LNT-045
module top(input logic clk, a, b, input logic [1:0] m, output logic y0, y1, y2, y3, q);
  logic t;
  always @(a or b) y0 = a & b;
  always @* y1 = a | b;
  always @(m[1] or a) begin
    t = m[1];
    y2 = t & a;
  end
  always_comb y3 = a ^ b;
  always @(posedge clk) q <= a & b;
endmodule
