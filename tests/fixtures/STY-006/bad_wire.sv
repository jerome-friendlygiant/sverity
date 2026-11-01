// sverity-args: --top top
// expect: STY-006 INFO 4
`default_nettype wire
module top(input logic a, output logic y);
  assign y = a;
endmodule
