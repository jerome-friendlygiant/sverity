// Two primary reset inputs promised to assert together, and one of them promised to start asserted: both are environment
// assumptions, reported as INFO and never checked against the RTL.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/group_state.json
// expect-count: CST-016 INFO 2
// expect-none: CST-016 WARNING
// expect-coverage: reset-contracts COMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
