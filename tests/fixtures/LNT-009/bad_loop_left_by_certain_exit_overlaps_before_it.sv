// sverity-args: --top top
// The break runs at i == 3 whatever c is, so slang lists the writes of iterations 0 to 2: v[1] is written there, and the second
// process writes it too.
// expect: LNT-009 ERROR 8
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 8; i++) begin if (c || i == 3) break; v[i] = c; end
  always @(c) v[1] = 1'b0;
  assign y = v;
endmodule
