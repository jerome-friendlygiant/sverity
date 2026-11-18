// sverity-args: --top top
// A clock gate: the clock ANDed with the output of a latch that is open while the clock is low. The enable cannot glitch the output,
// so the gated clock is advisory.
// expect: LNT-047 INFO 9
// expect-none: LNT-047 WARNING
module gate(input logic clk_i, en_i, output logic clk_o);
  logic en_q;
  always_latch if (!clk_i) en_q = en_i;
  assign clk_o = en_q & clk_i;
endmodule
module top(input logic clk, en, d, output logic q);
  logic gclk;
  gate u_gate(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always_ff @(posedge gclk) q <= d;
endmodule
