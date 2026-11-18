// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// `rst_q` may be a registered reset or a functional flag: whether `r` is gated by a functional signal (RDC-009) or combines two resets
// (RDC-008) is undecided, so neither is claimed as fact (Unresolved).
// expect-unresolved: RDC-009
// expect-unresolved: RDC-008
module top(input logic clka, rst_q, rst_n, d, output logic q);
  logic r;
  assign r = !rst_q & rst_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
