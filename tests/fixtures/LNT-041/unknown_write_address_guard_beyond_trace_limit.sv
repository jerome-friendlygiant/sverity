// sverity-args: --top top
// The guard is the address compared with 4, copied through six wires: past the depth the trace follows, it counts as possibly narrowing.
// expect-unresolved: LNT-041 5
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  logic g0, g1, g2, g3, g4, g5;
  assign g0 = wa < 4'd4;
  assign g1 = g0; assign g2 = g1; assign g3 = g2; assign g4 = g3; assign g5 = g4;
  always_ff @(posedge clk) if (we && g5) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
