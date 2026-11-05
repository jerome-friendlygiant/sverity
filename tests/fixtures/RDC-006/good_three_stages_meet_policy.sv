// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option RDC-006.min_stages=3
// Near miss of bad_single_stage under a 3-stage policy: the reset synchroniser has exactly three stages.
// expect-none: RDC-006
module top(input logic clka, arst_n, d, output logic q);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; s3 <= s2; end
  always_ff @(posedge clka or negedge s3) if (!s3) q <= 1'b0; else q <= d;
endmodule
