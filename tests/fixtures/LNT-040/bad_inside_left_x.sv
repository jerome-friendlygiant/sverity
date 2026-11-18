// sverity-args: --top top
// An X literal in the left operand of inside is not a wildcard: the match is X.
// expect: LNT-040 WARNING 5
module top(input logic [1:0] a, output logic y);
  assign y = 2'bx0 inside {a, 2'b00};
endmodule
