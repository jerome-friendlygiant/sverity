// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An X literal operand: the net's value is not decided by its reset inputs alone, so the combination is Unresolved.
// expect-unresolved: RDC-008
module top(input logic clka, rst_a_n, rst_b_n, d, output logic q);
  logic r;
  assign r = rst_a_n & rst_b_n & 1'bx;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
