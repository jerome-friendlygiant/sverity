// sverity-args: --top top
// A 3-bit address reaches 8 of the 16 words: words 8 to 15 are never driven, whatever the address is.
// expect: LNT-041 WARNING 7
// revert: logic [2:0] wa => logic [3:0] wa
// revert-expect-none: LNT-041
module top(input logic clk, we, input logic [2:0] wa, input logic [3:0] ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
