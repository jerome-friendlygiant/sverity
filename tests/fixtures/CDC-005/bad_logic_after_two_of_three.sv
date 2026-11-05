// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// Three stages are required and logic stops the chain at two: it is too short, and the logic does not excuse that.
// expect: CDC-005 ERROR
// expect-none: CDC-006
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, s2, consumer;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) consumer <= 1'b0; else consumer <= ~s2;
  assign q = consumer;
endmodule
