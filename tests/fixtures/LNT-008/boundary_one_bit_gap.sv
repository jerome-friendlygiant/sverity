// sverity-args: --top top
// expect: LNT-008 WARNING 4
module top(input logic [6:0] a, output logic [7:0] y);
  wire [7:0] v;
  assign v[6:0] = a;
  assign y = v;
endmodule
