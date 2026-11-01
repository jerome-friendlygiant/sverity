// sverity-args: --top top
// expect: LNT-040 WARNING 4
module top(input logic [3:0] a, output logic m);
  assign m = (a == 4'bx0);
endmodule
