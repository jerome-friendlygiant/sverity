// sverity-args: --top top
// comparisons between different signals, in-range constants and non-pure operands are not tautologies
// expect-none: LNT-038
module top(input logic [2:0] x, w, input logic signed [7:0] s, input logic a, b, output logic y0, y1, y2, y3, y4);
  assign y0 = x == w;
  assign y1 = x < 7;
  assign y2 = s >= -1;
  assign y3 = (x + 1) == x;
  assign y4 = a && !b;
endmodule
