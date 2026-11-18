// sverity-args: --top top
// A clock that a register or logic drives is not a copy of the writer's clock: the clocks stay unrelated.
// expect-unresolved: LNT-082 8
// expect-unresolved: LNT-082 9
module top(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  wire clk_gated = clk & en;
  always_ff @(posedge clk) mem[1] <= d;
  always_ff @(posedge clk_gated) q <= mem[1];
endmodule
