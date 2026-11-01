// sverity-args: --top top
// expect: LNT-040 WARNING 4
module top(input logic [3:0] a, b, output logic m);
  assign m = (a === b);
endmodule
