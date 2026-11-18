// sverity-args: --top top
// `clr` is data in the first instance of `st` and the asynchronous reset `arst` in the second, so the module is judged for each instance.
// `b` sits in a generate block and has no reset value in both copies of that block: a definite INFO, not an Unresolved WARNING that
// counts the generate scopes (or instances that lack the block) as instances that disagree.
// expect: LNT-067 INFO 10
// expect-none: LNT-067 WARNING
module st(input logic clk, arst, clr, d, output logic a, b, c);
  always_ff @(posedge clk) if (clr) a <= 1'b0; else a <= d;
  if (1) begin : g
    always_ff @(posedge clk or posedge arst) if (arst) c <= 1'b0; else begin c <= d; b <= d; end
  end
endmodule
module top(input logic clk, arst, go, d, output logic q, a0, a1, b0, b1, c0, c1);
  always_ff @(posedge clk or posedge arst) if (arst) q <= 1'b0; else q <= d;
  st u0(.clk(clk), .arst(arst), .clr(go), .d(d), .a(a0), .b(b0), .c(c0));
  st u1(.clk(clk), .arst(arst), .clr(arst), .d(d), .a(a1), .b(b1), .c(c1));
endmodule

// expect-confidence: LNT-067 established
