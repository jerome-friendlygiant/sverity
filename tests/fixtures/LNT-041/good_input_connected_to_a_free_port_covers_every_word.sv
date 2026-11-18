// sverity-args: --top top
// Lookalike of the narrowed instance: the parent forwards its own free 3-bit input, so every word can be written.
// expect-none: LNT-041
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa, .wd, .ra, .rd);
endmodule
