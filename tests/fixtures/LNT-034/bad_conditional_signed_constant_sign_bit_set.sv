// sverity-args: --top top
// 4'(9) is signed -7: the 8-bit result sign-extends it, so the arm is not the same value as an unsigned 9.
// revert: c ? 4'(9) : u => c ? 4'(7) : u
// revert-expect-none: LNT-034
// expect: LNT-034 WARNING 7
module top(input logic c, input logic [3:0] u, output logic [7:0] y);
  assign y = c ? 4'(9) : u;
endmodule
