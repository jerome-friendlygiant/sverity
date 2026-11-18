// sverity-args: --top top
// The only instance ties the enable to 1'b0, so the arm that writes the register is never selected: the register is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 1'b1: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b0), .d, .q);
endmodule
