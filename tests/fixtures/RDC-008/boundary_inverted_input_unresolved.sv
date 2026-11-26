// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two sources, but one enters inverted: the combination is outside the true-polarity And/Or class, so it sits exactly at the
// Heuristic/Unresolved edge and is reported Unresolved, never as a classified combination.
// expect-unresolved: RDC-008
module top(input logic clka, rst_a_n, rst_b_n, d, output logic q);
  logic r;
  assign r = ~rst_a_n & rst_b_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
