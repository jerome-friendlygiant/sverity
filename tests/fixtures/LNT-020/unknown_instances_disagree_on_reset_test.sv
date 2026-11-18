// sverity-args: --top top
// `clr` is data in the first instance of `st` and the asynchronous reset `arst` in the second: whether `seen` is reset-only and `b` has
// no reset value depends on the instance, so neither is claimed (Unresolved, naming the instances). With both instances on `arst` it is.
// expect-unresolved: LNT-067 12
// expect-unresolved: LNT-020 13
// expect-none: LNT-067 INFO
// revert: .clr(go) => .clr(arst)
// revert-expect-none: LNT-067 WARNING
module st(input logic clk, clr, d, output logic a, b, seen);
  always_ff @(posedge clk) begin
    if (clr) a <= 1'b0; else a <= d;
    b <= d;
    if (clr) seen <= 1'b1;
  end
endmodule
module top(input logic clk, arst, go, d, output logic q, a0, a1, b0, b1, s0, s1);
  always_ff @(posedge clk or posedge arst) if (arst) q <= 1'b0; else q <= d;
  st u0(.clk(clk), .clr(go), .d(d), .a(a0), .b(b0), .seen(s0));
  st u1(.clk(clk), .clr(arst), .d(d), .a(a1), .b(b1), .seen(s1));
endmodule
