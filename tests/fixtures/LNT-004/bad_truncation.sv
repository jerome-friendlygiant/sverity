// sverity-args: --top top
// expect: LNT-004 WARNING
module top(input logic [7:0] b, output logic [3:0] a);
  assign a = b;
endmodule
