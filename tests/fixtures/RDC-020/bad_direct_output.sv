// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The unreset bit q[1] is itself a primary output: the partial reset is visible on the interface, a WARNING (same as the equivalent output through a wire).
// expect: RDC-020 WARNING
module top(input logic clka, rn, d, e, output logic [1:0] q);
  always_ff @(posedge clka or negedge rn) if (!rn) q[0] <= 1'b0; else q[0] <= d;
  always_ff @(posedge clka) q[1] <= e;
endmodule
