// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The first inverter drives bit 0 of a bus too wide to track per bit, the second reads bit 1: one collapsed node stands for both,
// so the pair is not `~~clk` and the clock stays a logic root of its own.
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic clk, d, output logic a, b);
  wire [131072:0] inv;
  assign inv[0] = ~clk;
  wire out_clk = ~inv[1];
  always @(posedge clk) a <= d;
  always @(posedge out_clk) b <= a;
endmodule
