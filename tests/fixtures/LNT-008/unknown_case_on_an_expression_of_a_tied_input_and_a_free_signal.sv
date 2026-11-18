// sverity-args: --top top
// The selector masks the tied enable with a free signal: the ties do not give its value, so whether the arm is selected is not decided.
// expect-unresolved: LNT-008 5
module child(input logic clk, en, x, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en & x) 1'b1: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, x, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b0), .x, .d, .q);
endmodule
