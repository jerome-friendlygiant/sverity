// sverity-args: --top top
// Two tri-state buffers share one pad net, `bufif1` on the enable and `bufif0` on its complement: each output depends on its data
// and enable, so the net is no loop.
// expect-none: LNT-012
module top(input logic e, a, b, output wire y);
  bufif1 (y, a, e);
  bufif0 (y, b, e);
endmodule
