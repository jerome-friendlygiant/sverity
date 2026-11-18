// sverity-args: --top top
// Lookalike of the tied-off output write: the instance ties en to 1, so the write runs and the output is driven.
// expect-none: LNT-024
module leaf(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk) if (en == 1) q <= d;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(2'd1), .d, .q);
endmodule
