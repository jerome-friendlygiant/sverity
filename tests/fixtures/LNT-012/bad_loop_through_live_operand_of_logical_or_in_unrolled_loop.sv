// sverity-args: --top top
// Lookalike: `(i > 4) || fb[i]` has a constant 0 left operand, so the right one decides and closes the loop.
// expect: LNT-012 ERROR
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] fb;
  always_comb for (int i = 0; i < 4; i++) y[i] = x[i] ^ ((i > 4) || fb[i]);
  assign fb = y;
endmodule
