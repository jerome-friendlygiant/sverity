// sverity-args: --top top
// The condition compares the tied enable with a free signal: the tie does not decide it.
// expect-unresolved: LNT-008 5
module child(input logic clk, input logic [1:0] en, lim, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (en < lim) r <= d;
  assign q = r;
endmodule
module top(input logic clk, input logic [1:0] lim, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'd0), .lim, .d, .q);
endmodule
