// sverity-args: --top top
// Lookalike of the signed-alias defect: the instance ties en to 1, so gate == 1 holds and the write runs.
// expect-none: LNT-008
module leaf(input logic clk, input logic signed [1:0] en, output logic q);
  logic r;
  wire signed [1:0] gate = en;
  always_ff @(posedge clk) if (gate == 1) r <= 1'b1;
  assign q = r;
endmodule
module top(input logic clk, output logic q);
  leaf u(.clk, .en(2'sd1), .q);
endmodule
