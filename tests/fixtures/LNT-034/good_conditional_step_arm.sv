// sverity-args: --top top
// x - 1 is 32 bits only because of the unsized literal, and the target is as wide as x: only discarded upper bits differ.
// expect-none: LNT-034
module top(input logic c, input logic [15:0] x, y, output logic [15:0] o);
  assign o = c ? x - 1 : y;
endmodule
