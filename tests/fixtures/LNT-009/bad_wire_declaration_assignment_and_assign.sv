// sverity-args: --top top
// `wire w = a;` is a continuous driver, so the `assign` is a second one.
// expect: LNT-009 ERROR 6
module top(input logic a, b, output wire y);
  wire w = a;
  assign w = b;
  assign y = w;
endmodule
