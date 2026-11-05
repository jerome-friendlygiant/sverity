// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// Three stages are required and logic reads the second: an edge detector there reads too early, like any other tap.
// expect: CDC-007 ERROR
module top(input logic clka, clkb, d, output wire y);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin
    s1 <= a_q; s2 <= s1; s3 <= s2;
  end
  assign y = s2 ^ s3;
endmodule
