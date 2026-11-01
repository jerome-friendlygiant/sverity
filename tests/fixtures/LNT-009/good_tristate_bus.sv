// sverity-args: --top top
// Tri-state drivers (each can release the net) with provably exclusive enables (en versus !en) are a bus, not a conflict.
// expect-none: LNT-009
module top(input logic a, b, en, output wire t);
  assign t = en ? a : 1'bz;
  assign t = !en ? b : 1'bz;
endmodule
