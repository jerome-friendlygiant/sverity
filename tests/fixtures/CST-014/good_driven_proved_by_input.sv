// sverity-args: --top top --sdc @DIR@/good_driven_proved_by_input.sdc
// The driver proves the requested value once en is fixed, so the branch goes.
// expect: CST-014 WARNING
// expect-none: CST-009
// expect-none: CDC-028
// expect-coverage: case-analysis-modes INCOMPLETE
module top(input logic a, b, en, output logic q);
  wire m = en;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
