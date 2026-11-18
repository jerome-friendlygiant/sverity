// sverity-args: --top top
// The label 2'b1? matches the selector when its top bit is set. The only instance ties the selector to 2'b01, so the arm never runs and r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) casez (en) 2'b1?: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'b01), .d, .q);
endmodule
