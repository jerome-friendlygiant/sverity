// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unconstrained primary inputs feeding gated async reset logic (even with non-rst names like rn) must trigger RDC-002.
// expect: RDC-002 WARNING
// expect-unresolved: RDC-002
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rn, en, d, output logic q);
  logic g;
  assign g = rn & en;
  always_ff @(posedge clka or negedge g) if (!g) q <= 1'b0; else q <= d;
endmodule
