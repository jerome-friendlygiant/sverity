// sverity-args: --top top
// {a[6], a} repeats bit 6, not the sign bit 7 of the 8-bit signed a: the extension is wrong for negative a.
// expect: LNT-005 WARNING 5
module top(input logic signed [7:0] a, output logic signed [8:0] b);
  assign b = {a[6], a};
endmodule
