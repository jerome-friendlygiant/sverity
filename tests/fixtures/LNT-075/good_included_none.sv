// sverity-args: --top top -I@DIR@
// expect-none: LNT-075
`include "nettype_none.svh"
module top(input logic a, output logic y);
  assign y = a;
endmodule
