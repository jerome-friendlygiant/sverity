// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=2
// The minimum legal setting is two; a two-stage chain meets the threshold exactly.
// expect-none: CDC-005
// expect: CDC-003 INFO
module top(input logic clka, clkb, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
endmodule
