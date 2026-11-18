// sverity-args: --top top
// The latch is transparent while the clock is high, so the enable can change while the AND passes the clock: the gated clock can
// glitch. The same latch on the inverted clock is the boundary gate.
// expect: LNT-047 WARNING 11
// expect-none: LNT-047 INFO
// revert: if (clk) en_q = en; => if (!clk) en_q = en;
// revert-expect-none: LNT-047 WARNING
module top(input logic clk, en, d, output logic q);
  logic en_q, gclk;
  always_latch if (clk) en_q = en;
  assign gclk = en_q & clk;
  always_ff @(posedge gclk) q <= d;
endmodule
