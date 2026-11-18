// sverity-args: --top top
// The latch is also opened by a free input, so it can be transparent while the clock is high.
// expect: LNT-047 WARNING 8
// expect-none: LNT-047 INFO
module top(input logic clk, en, test, d, output logic q);
  logic en_q, gclk;
  always_latch if (!clk || test) en_q = en;
  assign gclk = en_q & clk;
  always_ff @(posedge gclk) q <= d;
endmodule
