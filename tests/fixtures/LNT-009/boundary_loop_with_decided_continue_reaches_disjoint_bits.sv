// sverity-args: --top top
// The loop with the decided `continue` reaches v[3:0] only, so the writer of v[7] cannot overlap it, whichever iterations run.
// expect-none: LNT-009
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 4; i++) begin if (i == 3) continue; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
