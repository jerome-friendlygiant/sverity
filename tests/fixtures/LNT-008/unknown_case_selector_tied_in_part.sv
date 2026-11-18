// sverity-args: --top top
// The only instance ties the top bit of the selector to 0 and leaves the low bit free: whether the arm 2'b10 is ever selected depends on the tie, which the analysis does not decide.
// expect-unresolved: LNT-008 5
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 2'b10: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, x, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en({1'b0, x}), .d, .q);
endmodule
