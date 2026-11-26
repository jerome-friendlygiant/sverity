// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The unreset bit v[1] is itself the asynchronous control of q: the stale value drives a reset pin, so the partial reset is a WARNING.
// expect: RDC-020 WARNING
module top(input logic clka, rn, d, e, output logic q);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rn) if (!rn) v[0] <= 1'b0; else v[0] <= d;
  always_ff @(posedge clka) v[1] <= e;
  always_ff @(posedge clka or negedge v[1]) if (!v[1]) q <= 1'b0; else q <= d;
endmodule
