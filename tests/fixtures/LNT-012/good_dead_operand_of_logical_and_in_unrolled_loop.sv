// sverity-args: --top top
// `(i > 0) && fb[i-1]`: for i = 0 the left operand is constant 0, so the right one is never read; the loop is a chain through fb.
// expect-none: LNT-012
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] fb;
  always_comb for (int i = 0; i < 4; i++) y[i] = x[i] ^ ((i > 0) && fb[i-1]);
  assign fb = y;
endmodule
