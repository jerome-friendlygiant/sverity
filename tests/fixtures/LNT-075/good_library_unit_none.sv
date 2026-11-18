// sverity-args: --top top -v @DIR@/library_cell_none.svh
// expect-none: LNT-075
`default_nettype none
module top(input logic a, output logic y);
  lib_cell u(.a(a), .y(y));
endmodule
