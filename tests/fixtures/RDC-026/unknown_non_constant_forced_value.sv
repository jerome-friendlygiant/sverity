// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The asynchronous clear forces the value of input v, which is not a constant, so the value forced on q is unknown. The finding must be Unresolved and the scope INCOMPLETE; priority between the two controls is not evaluated.
// expect: RDC-026 WARNING
// expect-unresolved: RDC-026
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, set_n, v, d, output logic q);
  always_ff @(posedge clka or negedge rst_n or negedge set_n) if (!rst_n) q <= v; else if (!set_n) q <= 1'b1; else q <= d;
endmodule
