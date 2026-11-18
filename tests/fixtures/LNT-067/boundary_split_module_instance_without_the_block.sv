// sverity-args: --top top
// `clr` differs between the instances, so the module is judged for each instance. The parameter keeps the block `g1` out of the middle
// instance: `b` has no reset value in `g1` of the two instances that have it. The instance without the block is not a copy of it and
// does not turn the finding into an Unresolved WARNING.
// expect: LNT-067 INFO 10
// expect-none: LNT-067 WARNING
module m #(parameter bit P = 0) (input logic clk, arst, clr, d, output logic a, b, c);
  always_ff @(posedge clk) if (clr) a <= 1'b0; else a <= d;
  if (P) begin : g1
    always_ff @(posedge clk or posedge arst) if (arst) c <= 1'b0; else begin c <= d; b <= d; end
  end else begin : g0
    always_ff @(posedge clk or posedge arst) if (arst) begin c <= 1'b0; b <= 1'b0; end else begin c <= d; b <= d; end
  end
endmodule
module top(input logic clk, arst, go, d, output logic q, a0, a1, a2, b0, b1, b2, c0, c1, c2);
  always_ff @(posedge clk or posedge arst) if (arst) q <= 1'b0; else q <= d;
  m #(.P(1)) u0(.clk(clk), .arst(arst), .clr(arst), .d(d), .a(a0), .b(b0), .c(c0));
  m #(.P(0)) u1(.clk(clk), .arst(arst), .clr(go), .d(d), .a(a1), .b(b1), .c(c1));
  m #(.P(1)) u2(.clk(clk), .arst(arst), .clr(arst), .d(d), .a(a2), .b(b2), .c(c2));
endmodule
