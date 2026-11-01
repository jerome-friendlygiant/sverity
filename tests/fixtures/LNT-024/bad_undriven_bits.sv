// sverity-args: --top top
// expect: LNT-024 WARNING 3
module top(input logic a, output logic [3:0] y);
  assign y[1:0] = {2{a}};
endmodule
