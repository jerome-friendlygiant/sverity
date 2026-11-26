// The order names a clock that the SDC does not declare: Unresolved with the clock reason, scope INCOMPLETE.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_clock_order.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
