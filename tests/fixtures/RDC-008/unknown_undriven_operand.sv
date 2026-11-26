// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The third operand is a net nothing drives: its value is unknown, so the two-reset combination cannot be decided. Unresolved, never dropped.
// expect-unresolved: RDC-008
module top(input logic clka, rst_a_n, rst_b_n, d, output logic q);
  logic r;
  wire missing;
  assign r = rst_a_n & rst_b_n & missing;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
