// sverity-args: --top top
// The read is registered on the writer's own clock: the RTL defines the old data (read first), so no collision obligation.
// expect-none: LNT-082 WARNING
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
