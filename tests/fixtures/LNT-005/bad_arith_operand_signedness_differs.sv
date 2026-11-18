// sverity-args: --top top
// expect: LNT-005 WARNING
module top(input logic [7:0] b, input logic signed [7:0] c, output logic [7:0] y);
  assign y = b + c;
endmodule
