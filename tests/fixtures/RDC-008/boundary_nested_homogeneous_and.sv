// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A nested AND of three active-low resets is one homogeneous form (every operator is an AND): classified, Heuristic, asserting on any source.
// expect: RDC-008 WARNING
// expect-confidence: RDC-008 heuristic
module top(input logic clka, rst_a_n, rst_b_n, rst_c_n, d, output logic q);
  logic r;
  wire mid = rst_a_n & rst_b_n;
  assign r = mid & rst_c_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
