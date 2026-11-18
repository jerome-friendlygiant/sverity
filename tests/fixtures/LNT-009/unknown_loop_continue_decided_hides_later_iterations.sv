// sverity-args: --top top
// `continue` at i == 3 is decided: slang then drops every iteration after it from its drivers, so they cannot place this loop's writes.
// expect: LNT-009 WARNING 7
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 8; i++) begin if (i == 3) continue; v[i] = c; end
  always @(c) v[6] = 1'b0;
  assign y = v;
endmodule
