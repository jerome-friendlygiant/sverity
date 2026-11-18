// sverity-args: --top top
// A default value then a conditional copy is a gate, not a forward: the output follows the clock only while en holds.
// expect: LNT-027 WARNING 7
// expect-none: LNT-027 INFO
module top(input logic clk, d, en, output logic q, clk_o);
  always_ff @(posedge clk) q <= d;
  always_comb begin clk_o = 1'b0; if (en) clk_o = clk; end
endmodule
