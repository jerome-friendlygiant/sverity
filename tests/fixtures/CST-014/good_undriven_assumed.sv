// sverity-args: --top top --sdc @DIR@/good_undriven_assumed.sdc
// Nothing drives m, so the constraint is the only source of its value: applied, and the run says it is an assumption.
// expect: CST-014 WARNING
// expect-none: CST-009
// expect: LNT-008 WARNING
// expect-none: CDC-028
// expect-coverage: case-analysis-modes INCOMPLETE
module top(input logic a, b, output logic q);
  logic m;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
