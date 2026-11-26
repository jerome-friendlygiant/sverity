// An SDC case analysis is present: every clause is Unresolved for 1.0, whatever the binding.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --sdc @DIR@/case.sdc --config @DIR@/case_clauses.json
// expect-count: CST-016 WARNING 2
// expect-confidence: CST-016 unresolved
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
