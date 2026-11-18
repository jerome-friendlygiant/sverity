// sverity-args: --top top
// The case compares the two-bit enable with 2'd1 at two bits. The only instance ties it to 2'd0, so the writing arm is never selected and r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 2'd1: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'd0), .d, .q);
endmodule
