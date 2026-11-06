// sverity-args: --top top --sdc @DIR@/wildcard_master.sdc
// -master_clock takes a clock collection: mast* selects master, which the group then separates from its generated clock.
// expect: CST-003 WARNING
// expect-none: CST-002
module top(input logic a, output logic q);
  always_ff @(posedge a) q <= ~q;
endmodule
