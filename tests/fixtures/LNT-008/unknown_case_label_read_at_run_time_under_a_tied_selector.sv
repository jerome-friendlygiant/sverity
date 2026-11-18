// sverity-args: --top top
// The selector is tied but the label is a run-time value: which arm matches is not decided.
// expect-unresolved: LNT-008 5
module child(input logic clk, input logic [1:0] en, lab, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) lab: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [1:0] lab, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'd0), .lab, .d, .q);
endmodule
