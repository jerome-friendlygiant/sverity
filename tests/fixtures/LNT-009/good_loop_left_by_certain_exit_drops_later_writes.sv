// sverity-args: --top top
// `c || i == 3` exits at i == 3 whatever c is: slang drops the iterations after it exactly, so no write of the loop reaches v[7].
// expect-none: LNT-009
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 8; i++) begin if (c || i == 3) break; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
