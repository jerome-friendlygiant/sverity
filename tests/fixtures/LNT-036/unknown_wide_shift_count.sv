// sverity-args: --top top
// expect: LNT-036 WARNING
module top(input logic [63:0] x, output logic [7:0] y);
  assign y = 8'd1 << (x+64'd1);
endmodule
