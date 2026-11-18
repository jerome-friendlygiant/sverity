// sverity-args: --top top
// The selector is tied to 1 and compared with the label 2'd2 at two bits: it never matches, the empty default runs and r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 2'd2: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b1), .d, .q);
endmodule
