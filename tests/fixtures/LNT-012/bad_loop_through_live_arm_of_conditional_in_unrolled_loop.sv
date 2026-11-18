// sverity-args: --top top
// Lookalike of the dead-arm cases: the live arm reads fb[i], which is y[i] itself, so every iteration closes a loop.
// expect: LNT-012 ERROR
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] fb;
  always_comb for (int i = 0; i < 4; i++) y[i] = x[i] ^ (i > 0 ? fb[i] : 1'b0);
  assign fb = y;
endmodule
