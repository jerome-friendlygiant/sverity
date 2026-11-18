// sverity-args: --top top
// slang's own conflict check for always_comb stops at the decided `continue` (i == 2), so it never sees v[7] written by this loop.
// expect: LNT-009 WARNING 7
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always_comb for (int i = 0; i < 8; i++) begin if (i == 2) continue; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
