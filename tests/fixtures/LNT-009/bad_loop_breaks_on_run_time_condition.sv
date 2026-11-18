// sverity-args: --top top
// The break depends on an input: iteration 0 always writes v[0] before it, so the overlap with the other process is certain.
// expect: LNT-009 ERROR 7
module top(input logic c, input logic [7:0] x, output logic [7:0] y);
  logic [7:0] v;
  always @(c, x) for (int i = 0; i < 8; i++) begin v[i] = c; if (x[i]) break; end
  always @(c) v[0] = 1'b0;
  assign y = v;
endmodule
