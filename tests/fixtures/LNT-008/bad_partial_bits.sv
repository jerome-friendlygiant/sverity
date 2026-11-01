// sverity-args: --top top
// expect: LNT-008 WARNING 4
module top(input logic a, output logic y);
  wire [3:0] w;
  assign w[1:0] = {2{a}};
  assign y = w[0];
endmodule
