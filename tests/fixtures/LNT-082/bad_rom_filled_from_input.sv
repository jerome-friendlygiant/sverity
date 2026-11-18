// sverity-args: --top top
// The initial loop copies an input, so the fill is not a constant table: the memory keeps a writer.
// expect: LNT-082 WARNING 7
module top(input logic clk, input logic [3:0] a, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] rom [16];
  initial for (int i = 0; i < 16; i++) rom[i] = d;
  always_ff @(posedge clk) q <= rom[a];
endmodule
