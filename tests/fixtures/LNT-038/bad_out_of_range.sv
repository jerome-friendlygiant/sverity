// sverity-args: --top top
// constants outside the operand's value range, and unsigned comparisons with 0
// expect: LNT-038 WARNING 10
// expect: LNT-038 WARNING 11
// expect: LNT-038 WARNING 12
// expect: LNT-038 WARNING 13
// expect: LNT-038 WARNING 14
module top(input logic [2:0] x, input logic signed [7:0] s, input logic u1,
           output logic y0, y1, y2, y3, y4);
  assign y0 = x == 9;
  assign y1 = x >= 0;
  assign y2 = x < 0;
  assign y3 = s > 127;
  assign y4 = u1 > 1;
endmodule
