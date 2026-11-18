// sverity-args: --top top
// The top ties a two-bit port to 2'b10 and the middle module passes only its low bit to the one-bit enable: it is 0, so r is never driven.
// expect: LNT-008 WARNING 5
module leaf(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (en) r <= d;
  assign q = r;
endmodule
module mid(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(en[0]), .d, .q);
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  mid m(.clk, .en(2'b10), .d, .q);
endmodule
