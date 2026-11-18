// sverity-args: --top top
// Plain P3: {a[7], a} repeats the sign bit of the signed byte a above it, so the 9-bit signed result equals signed a.
// expect: LNT-005 INFO 6
// expect-none: LNT-005 WARNING
module top(input logic signed [7:0] a, output logic signed [8:0] b);
  assign b = {a[7], a};
endmodule
