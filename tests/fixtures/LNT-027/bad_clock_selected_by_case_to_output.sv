// sverity-args: --top top
// One case arm copies the clock and another ties the output high: a decoded gate, logic on the clock path.
// expect: LNT-027 WARNING 7
// expect-none: LNT-027 INFO
module top(input logic clk, d, input logic [1:0] m, output logic q, clk_o);
  always_ff @(posedge clk) q <= d;
  always_comb case (m) 2'd0: clk_o = clk; default: clk_o = 1'b1; endcase
endmodule
