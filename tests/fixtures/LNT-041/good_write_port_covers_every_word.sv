// sverity-args: --top top
// A 4-bit address reaches all 16 words of the memory, so every member can be driven.
// expect-none: LNT-041
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
