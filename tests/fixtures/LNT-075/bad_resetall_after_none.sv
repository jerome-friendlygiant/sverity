// sverity-args: --top top
// expect: LNT-075 INFO 5
`default_nettype none
`resetall
module top(input logic a, output logic y);
  assign y = a;
endmodule
