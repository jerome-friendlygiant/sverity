// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Edge of the synchronous candidate: the if branch assigns a constant and a non-constant, so it is not a candidate and the design has no reset root.
// expect-none: RDC-001
module top(input logic clka, srst_n, d, output logic q, r);
  always_ff @(posedge clka) if (!srst_n) begin q <= 1'b0; r <= d; end else begin q <= d; r <= d; end
endmodule
