// sverity-args: --top top
// expect: LNT-026 WARNING
module top(input logic [7:0] a, output logic [7:0] y);
  assign y = a << 40;
endmodule
