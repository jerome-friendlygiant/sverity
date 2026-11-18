// sverity-args: --top top
// The 32-bit target keeps the borrow of x - 1, which the 16-bit arm cannot show.
// revert: output logic [31:0] o => output logic [15:0] o
// revert-expect-none: LNT-034
// expect: LNT-034 WARNING 7
module top(input logic c, input logic [15:0] x, y, output logic [31:0] o);
  assign o = c ? x - 1 : y;
endmodule
