// sverity-args: --top top
// The same gate with the enable taken straight from a register: it changes while the clock is high, and the output glitches.
// expect: LNT-047 WARNING 9
// revert: always_ff @(posedge clk_i) en_q <= en_i; => always_latch if (!clk_i) en_q = en_i;
// revert-expect-none: LNT-047 WARNING
module gate(input logic clk_i, en_i, output logic clk_o);
  logic en_q;
  always_ff @(posedge clk_i) en_q <= en_i;
  assign clk_o = en_q & clk_i;
endmodule
module top(input logic clk, en, d, output logic q);
  logic gclk;
  gate u_gate(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always_ff @(posedge gclk) q <= d;
endmodule
