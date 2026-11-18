// sverity-args: --top top
// The break is decided through a local copy of the loop variable, which slang's drivers do not follow: they name all 8 iterations, so overlap with v[7] cannot be excluded.
// expect: LNT-009 WARNING 7
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 8; i++) begin automatic int t = i; if (t == 3) break; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
