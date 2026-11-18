// sverity-args: --top top -v @DIR@/library_cell_wire.svh
// expect: LNT-075 INFO 1
`default_nettype none
module top(input logic a, output logic y);
  lib_cell u(.a(a), .y(y));
endmodule
