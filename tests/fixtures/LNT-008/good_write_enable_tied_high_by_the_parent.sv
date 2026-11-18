// sverity-args: --top top
// Lookalike of the tied-off enable: tied to 1'b1 the write is live, and the free address reaches every word.
// expect-none: LNT-008
// expect-none: LNT-041
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we(1'b1), .wa, .wd, .ra, .rd);
endmodule
