// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// The tap on s2 comes first in the source: the exact next stage s3 still continues the chain, so the tap is reported.
// expect: CDC-007 ERROR
module top(input logic clka, clkb, d, output logic q);
  logic a_q, s1, s2, s3, consumer;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin
    s1 <= a_q;
    s2 <= s1;
    consumer <= ~s2;
    s3 <= s2;
  end
  assign q = s3 ^ consumer;
endmodule
