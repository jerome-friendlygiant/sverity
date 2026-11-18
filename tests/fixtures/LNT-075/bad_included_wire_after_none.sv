// sverity-args: --top top -I@DIR@
// expect: LNT-075 INFO 5
`default_nettype none
`include "nettype_wire.svh"
module top(input logic a, output logic y);
  assign y = a;
endmodule
