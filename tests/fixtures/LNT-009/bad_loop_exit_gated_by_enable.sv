// sverity-args: --top top
// `en && i == 3` exits at i == 3 only when en is set; with en clear every iteration writes, so v[7] is written by the loop.
// expect: LNT-009 ERROR 7
module top(input logic c, input logic en, output logic [7:0] y);
  logic [7:0] v;
  always @(c, en) for (int i = 0; i < 8; i++) begin if (en && i == 3) break; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
