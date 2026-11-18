// sverity-args: --top top --single-unit @DIR@/nettype_none.svh
// expect-none: LNT-075
module top(input logic a, output logic y);
  assign y = a;
endmodule
