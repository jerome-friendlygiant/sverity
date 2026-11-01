// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// revert: logic a_q, s1, s2; => logic a_q, s1, s2, s3;
// revert: {s2, s1} <= 2'b00; else {s2, s1} <= {s1, a_q}; => {s3, s2, s1} <= 3'b000; else {s3, s2, s1} <= {s2, s1, a_q};
// revert: assign q = s2; => assign q = s3;
// revert-expect-none: CDC-005 ERROR
// A policy of 3 stages makes a 2-flop synchroniser too short.
// expect: CDC-005 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b00; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule
