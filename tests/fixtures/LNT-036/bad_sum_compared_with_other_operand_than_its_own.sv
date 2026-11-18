// sverity-args: --top top
// Lookalike of the overflow test: the sum is compared with a third signal, so the lost carry changes the answer.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, c, output logic lt);
  assign lt = (a + b) < c;
endmodule
