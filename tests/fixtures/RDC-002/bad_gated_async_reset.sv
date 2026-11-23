// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An unconstrained primary reset input feeding gated async reset logic (g = rst_n & en) must still trigger RDC-002.
// expect: RDC-002 WARNING
// expect-unresolved: RDC-002
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, en, d, output logic q);
  logic g;
  assign g = rst_n & en;
  always_ff @(posedge clka or negedge g) if (!g) q <= 1'b0; else q <= d;
endmodule
