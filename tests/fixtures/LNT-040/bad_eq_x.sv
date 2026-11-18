// revert: 4'bx0 => 4'b00
// revert-expect-none: LNT-040
// sverity-args: --top top
// expect: LNT-040 WARNING 6
module top(input logic [3:0] a, output logic m);
  assign m = (a == 4'bx0);
endmodule
