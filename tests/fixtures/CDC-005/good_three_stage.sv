// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// expect-none: CDC-005
// expect: CDC-003 INFO
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= 3'b000; else {s3, s2, s1} <= {s2, s1, a_q};
  assign q = s3;
endmodule
