// sverity-args: --top top
// A right shift lets the upper bits of the wider arm flow down into the bits the target keeps.
// revert: (x >> 1) + 1 => x + 1
// revert-expect-none: LNT-034
// expect: LNT-034 WARNING 7
module top(input logic c, input logic [15:0] x, y, output logic [15:0] o);
  assign o = c ? (x >> 1) + 1 : y;
endmodule
