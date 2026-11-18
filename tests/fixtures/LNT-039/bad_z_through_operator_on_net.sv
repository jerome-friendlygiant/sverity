// sverity-args: --top top
// A 'z under an operator is X in the result, not high-impedance, so each assignment to a net is a WARNING (not the tri-state INFO).
// expect: LNT-039 WARNING 12
// expect: LNT-039 WARNING 13
// expect: LNT-039 WARNING 14
// expect: LNT-039 WARNING 15
// expect: LNT-039 WARNING 16
// expect-count: LNT-039 WARNING 5
// expect-none: LNT-039 INFO
module top(input logic a, input logic [1:0] k, input logic [3:0] m, output wire y1, y2, y3, y4, y5);
  localparam logic HIZ = 1'bz;
  assign y1 = a & 1'bz;
  assign y2 = a & HIZ;
  assign y3 = ~HIZ;
  assign y4 = HIZ + 1'b1;
  assign y5 = m[k ^ {1'b0, HIZ}];
endmodule
