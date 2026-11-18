// sverity-args: --top top
// A bufif1 output can release the net, like a 'z driver: with an `assign` on the same net the overlap is Unresolved.
// expect: LNT-009 WARNING 6
module top(input logic a, b, e, output wire y);
  bufif1 (y, a, e);
  assign y = b;
endmodule
