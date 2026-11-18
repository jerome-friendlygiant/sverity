// sverity-args: --top top
// The shift is LNT-035's, but the sum under it is evaluated at 8 bits and loses its carry before the shift: it is its own site.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, c, input logic [2:0] s, output logic gt);
  assign gt = ((a + b) << s) > c;
endmodule
