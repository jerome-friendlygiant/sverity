// sverity-args: --top top
// the constant comes from a parameter: the result is configuration dependent, so Info
// expect: LNT-038 INFO 5
module top #(parameter int DEPTH = 9) (input logic [2:0] x, output logic y);
  assign y = x == DEPTH;
endmodule
