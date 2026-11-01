// sverity-args: --top top
// expect-none: STY-006
`default_nettype none
module top(input logic a, output logic y);
  assign y = a;
endmodule
