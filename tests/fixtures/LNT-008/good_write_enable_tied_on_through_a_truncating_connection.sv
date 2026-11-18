// sverity-args: --top top
// The same chain with the two-bit port tied to 2'b01: the low bit is 1 and the leaf writes r.
// expect-none: LNT-008
module leaf(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (en) r <= d;
  assign q = r;
endmodule
module mid(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(en[0]), .d, .q);
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  mid m(.clk, .en(2'b01), .d, .q);
endmodule
