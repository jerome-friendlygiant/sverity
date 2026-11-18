// sverity-args: --top top
// Only `< 0` and `<= -1` against a direct unsigned subtraction are reported: other bounds and signed operands are not.
// expect: LNT-055 WARNING 6
// expect-count: LNT-055 WARNING 1
module top(input logic [7:0] a, b, output logic n1, n2, n3, n4);
  assign n1 = ((a - b) < 0);        // unsigned difference below zero
  assign n2 = ((a - b) < 1);        // another bound: not tracked
  assign n3 = ((a - b) == 0);       // equality: no finding
  assign n4 = ((a - b) > 0);        // wrong direction: no finding
endmodule
