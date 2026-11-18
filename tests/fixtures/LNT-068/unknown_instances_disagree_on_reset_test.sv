// sverity-args: --top top
// The first block of `st` resets its register only in the instance where `clr` is the asynchronous reset `arst`: whether the second block
// lacks a reset that its module's other block has depends on the instance (Unresolved, naming them). With both instances on `arst` it is
// the usual INFO.
// expect-unresolved: LNT-068 11
// expect-none: LNT-068 INFO
// revert: .clr(go) => .clr(arst)
// revert-expect-none: LNT-068 WARNING
module st(input logic clk, clr, d, output logic a, b);
  always_ff @(posedge clk) if (clr) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) b <= d;
endmodule
module top(input logic clk, arst, go, d, output logic q, a0, a1, b0, b1);
  always_ff @(posedge clk or posedge arst) if (arst) q <= 1'b0; else q <= d;
  st u0(.clk(clk), .clr(go), .d(d), .a(a0), .b(b0));
  st u1(.clk(clk), .clr(arst), .d(d), .a(a1), .b(b1));
endmodule
