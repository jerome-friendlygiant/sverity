// sverity-args: --top top
// The gate's output terminal drives `y`; the `assign` drives it too.
// expect: LNT-009 ERROR 6
module top(input logic a, b, output wire y);
  and (y, a, b);
  assign y = a;
endmodule
