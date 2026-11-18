// sverity-args: --top top
// bufif1 and notif1 on one enable are enabled together: not exclusive, so Unresolved.
// expect: LNT-009 WARNING 6
module top(input logic a, b, e, output wire y);
  bufif1 (y, a, e);
  notif1 (y, b, e);
endmodule
