// sverity-args: --top top
// Lookalike of the modular difference: a signed difference can overflow, and the comparison then reads the wrapped value.
// expect: LNT-036 WARNING 5
module top(input logic signed [7:0] a, b, c, output logic lt);
  assign lt = (a - b) < c;
endmodule
