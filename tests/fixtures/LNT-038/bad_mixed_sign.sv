// sverity-args: --top top
// a signed operand against an unsigned constant is compared unsigned: s >= 0u is always true;
// -1 is converted to the unsigned comparison width, so u == -1 can never match a 3-bit u
// expect: LNT-038 WARNING 7
// expect: LNT-038 WARNING 8
module top(input logic signed [3:0] s, input logic [2:0] u, output logic y, z);
  assign y = s >= 4'd0;
  assign z = u == -1;
endmodule
