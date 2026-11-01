// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Latch-based gate: the enable latch is transparent while the clock is low.
// expect: CDC-020 INFO
// expect-none: CDC-020 WARNING
module top(input logic clka, en_d, rst_n, d, output logic q);
  logic en_q, en_l, gclk;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) en_q <= 1'b0; else en_q <= en_d;
  always_latch if (!clka) en_l = en_q;
  assign gclk = clka & en_l;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
