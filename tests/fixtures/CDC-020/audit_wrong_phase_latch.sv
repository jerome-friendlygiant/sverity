// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-020 WARNING
// expect-none: CDC-020 INFO
// Regression from the audit: a latch that is transparent while the clock is HIGH is not a clock gate.
// Clock gate whose enable latch is transparent while clk is HIGH (wrong phase): enable changes
// pass straight to the AND gate during the high phase and truncate/glitch the gated clock.
module top(input logic clka, en_in, d, output logic q);
  logic en_q, en_l, gclk;
  always_ff @(posedge clka) en_q <= en_in;
  always_latch if (clka) en_l = en_q;     // should be if (!clka)
  assign gclk = clka & en_l;
  always_ff @(posedge gclk) q <= d;
endmodule
