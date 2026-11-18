// sverity-args: --top top
// expect: LNT-004 WARNING
module top(input logic [3:0] a, input logic [7:0] b, output logic [7:0] y);
  assign y = a + b;
endmodule
