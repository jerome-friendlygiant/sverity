// sverity-args: --top top
// bufif1 and bufif0 with the same enable signal are never enabled together: a bus by construction.
// expect-none: LNT-009
module top(input logic a, b, e, output wire y);
  bufif1 (y, a, e);
  bufif0 (y, b, e);
endmodule
