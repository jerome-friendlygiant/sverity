// sverity-args: --top top
// The label 2'b1? matches the selector 2'b10 the only instance ties: the arm runs and r is driven.
// expect-none: LNT-008
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) casez (en) 2'b1?: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'b10), .d, .q);
endmodule
