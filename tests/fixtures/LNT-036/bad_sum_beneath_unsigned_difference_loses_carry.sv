// sverity-args: --top top
// The difference is modular on purpose, but the sum beneath it is its own site: a + b is evaluated at 8 bits and loses its carry before the subtraction.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, c, d, output logic lt);
  assign lt = (a + b - c) < d;
endmodule
