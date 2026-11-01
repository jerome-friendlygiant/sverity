// sverity-args: --top top
// expect: LNT-035 WARNING
module top(input logic signed [7:0] s, output logic signed [7:0] y);
  assign y = s >> 1;
endmodule
