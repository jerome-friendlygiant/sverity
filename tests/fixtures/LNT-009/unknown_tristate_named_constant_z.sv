// sverity-args: --top top
// The release value is a parameter that is all z: the drivers are tri-state, so the overlap is Unresolved, not a proven conflict.
// expect: LNT-009 WARNING 6
module top #(parameter logic Z = 1'bz)(input logic e1, e2, a, b, output wire y);
  assign y = e1 ? a : Z;
  assign y = e2 ? b : Z;
endmodule
