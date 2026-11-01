// sverity-args: --top top
// constants on the edge of the range are still possible values: no finding
// expect-none: LNT-038
module top(input logic [2:0] x, input logic signed [7:0] s, output logic y0, y1, y2, y3);
  assign y0 = x == 7;
  assign y1 = x <= 6;
  assign y2 = s > -128;
  assign y3 = s == 127;
endmodule
