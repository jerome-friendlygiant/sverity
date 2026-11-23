// sverity-args: --top top --sdc @DIR@/netlist_selector.sdc
// Unmatched netlist-style selectors in path exceptions must be disclosed under CST-002 and mark constraints INCOMPLETE.
// expect: CST-002 WARNING
// expect-unresolved: CST-002
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule
