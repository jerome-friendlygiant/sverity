// sverity-args: --top top
// The only instance ties the top bit of the two-bit mode to 0 and leaves the low bit free: whether the writing arm is ever selected is not decided.
// expect-unresolved: LNT-041 5
module child(input logic clk, input logic [1:0] mode, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) case (mode) 2'd2: mem[wa] <= wd; default: ; endcase
  assign rd = mem[ra];
endmodule
module top(input logic clk, x, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .mode({1'b0, x}), .wa, .wd, .ra, .rd);
endmodule
