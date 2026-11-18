// sverity-args: --top top
// A signed arm that is a constant whose sign bit is clear (a size cast of a small value) extends the same way as the unsigned arm.
// expect-none: LNT-034
module top #(parameter int unsigned W = 8) (input logic c, input logic [W-1:0] u, output logic [W-1:0] y, z);
  assign y = c ? W'(0) : u;
  assign z = c ? W'(63) : u;
endmodule
