// sverity-args: --top top
// Two drivers of equal weak strength are unordered: they conflict like two strong drivers.
// expect: LNT-009 ERROR 6
module top(input logic a, b, output wire w);
  assign (weak0, weak1) w = a;
  assign (weak0, weak1) w = b;
endmodule
