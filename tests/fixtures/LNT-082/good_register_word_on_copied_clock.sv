// sverity-args: --top top
// The reader's clock is a copy of the writer's clock, and one reader is on the inversion of it: one clock, so no cross-clock access.
// expect-none: LNT-082
module top(input logic clk, input logic [7:0] d, output logic [7:0] q, r);
  logic [7:0] mem [4];
  wire clk_copy = clk;
  wire clk_n = ~clk;
  always_ff @(posedge clk) mem[1] <= d;
  always_ff @(posedge clk_copy) q <= mem[1];
  always_ff @(posedge clk_n) r <= mem[1];
endmodule
