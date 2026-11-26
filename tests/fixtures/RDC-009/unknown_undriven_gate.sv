// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The gate operand is a net nothing drives: whether it is a reset or a functional signal is not known, so the gating is Unresolved, never dropped.
// expect-unresolved: RDC-009
module top(input logic clka, rst_n, d, output logic q);
  logic r;
  wire missing;
  assign r = rst_n & missing;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
