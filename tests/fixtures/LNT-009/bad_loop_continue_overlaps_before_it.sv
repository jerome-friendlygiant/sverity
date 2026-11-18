// sverity-args: --top top
// slang drops the iterations after the `continue` at i == 3, but iteration 0 runs before it and writes v[0], which the second
// process also writes.
// expect: LNT-009 ERROR 8
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 8; i++) begin if (i == 3) continue; v[i] = c; end
  always @(c) v[0] = 1'b0;
  assign y = v;
endmodule
