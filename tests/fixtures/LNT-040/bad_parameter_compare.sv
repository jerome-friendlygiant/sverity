// sverity-args: --top top
// A parameter holding X bits compared with == is the comparison with the literal: the result is X.
// expect: LNT-040 WARNING 6
module top(input logic [1:0] a, output logic y);
  localparam logic [1:0] DC = 2'bx1;
  assign y = (a == DC);
endmodule
