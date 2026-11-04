// sverity-args: --top top --sdc @DIR@/bad_hidden_crossing.sdc
// expect: CST-014 WARNING
// expect-none: CDC-003
// expect-coverage: case-analysis-modes INCOMPLETE
module top(input logic a, b, mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode) q <= source_q;
endmodule
