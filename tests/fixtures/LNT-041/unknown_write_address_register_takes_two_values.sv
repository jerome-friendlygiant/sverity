// sverity-args: --top top
// The address register only ever holds 0 or 1: words 2..15 are never written, and the index type's range does not show it.
// expect-unresolved: LNT-041 5
module top(input logic clk, we, t, input logic [3:0] ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  logic [3:0] wa;
  always_ff @(posedge clk) wa <= t ? 4'd0 : 4'd1;
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
