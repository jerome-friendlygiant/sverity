// sverity-args: --top top
// expect-none: LNT-034
module top(input logic c, input logic [7:0] a, b, input logic signed [7:0] s, t, output logic [7:0] o1, o2, o3, o4);
  assign o1 = c ? a : b;         // same width and sign
  assign o2 = c ? s : t;         // both signed
  assign o3 = c ? a : 0;         // unsized decimal, narrower arm: not reported
  assign o4 = c ? s : 8'sd0;     // signed literal, non-negative
endmodule
