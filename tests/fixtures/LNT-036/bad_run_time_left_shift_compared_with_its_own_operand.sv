// sverity-args: --top top
// Bits shifted out of a are lost before the compare, and a shift is no overflow idiom: (a << s) < a is not exempt.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, input logic [2:0] s, output logic lt);
  assign lt = (a << s) < a;
endmodule
