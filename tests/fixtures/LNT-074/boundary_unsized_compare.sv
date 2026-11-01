// sverity-args: --top top
// expect: LNT-074 INFO 6
// expect-count: LNT-074 INFO 1
module top(input logic [63:0] w, input logic [31:0] n, output logic h1, h2, h3);
  assign h1 = (n == 5);          // 32-bit operand: no finding
  assign h2 = (w == 5);          // 64-bit operand: unsized literal is 32 bits before extension
  assign h3 = (w == '0);         // fill literal: no finding
endmodule
