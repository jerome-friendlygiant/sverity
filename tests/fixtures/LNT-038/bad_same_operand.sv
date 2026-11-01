// sverity-args: --top top
// a comparison of an operand with itself is decided for every known value
// expect: LNT-038 WARNING 6
// expect: LNT-038 WARNING 7
module top(input logic [3:0] x, input logic a, output logic y, z);
  assign y = x == x;
  assign z = a && !a;
endmodule
