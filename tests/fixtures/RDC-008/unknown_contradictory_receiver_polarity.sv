// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One receiver resets on r active-high and another active-low: RDC-028 reports the conflict, and the combination's assertion condition is Unresolved rather than one receiver's polarity applied to all.
// expect: RDC-028 ERROR
// expect-unresolved: RDC-008
module top(input logic clka, rst_n, por_rst_n, d, output logic p, q);
  logic r;
  always_ff @(posedge clka or posedge r) if (r) p <= 1'b0; else p <= d;
  assign r = rst_n & por_rst_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
