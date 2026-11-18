// sverity-args: --top top
// The gated clock is the clock ANDed with a latch output, not a register's output: a register on it sees data of the same clock tree.
// expect: LNT-047 INFO 9
// expect-none: LNT-047 WARNING
module top(input logic clk, en, output logic [7:0] cap);
  logic en_q, gclk;
  logic [7:0] d;
  always_latch if (!clk) en_q = en;
  assign gclk = en_q & clk;
  always_ff @(posedge clk) d <= d + 1'b1;
  always_ff @(posedge gclk) cap <= d;
endmodule
