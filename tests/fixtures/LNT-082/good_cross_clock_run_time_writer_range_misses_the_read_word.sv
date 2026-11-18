// sverity-args: --top top
// The writer's index is run-time but its value range is 0..3 and the other clock reads word 7: no word is shared, so no cross-clock obligation (the collision obligation stays).
// expect-count: LNT-082 WARNING 1
// expect-unresolved: LNT-082 8
module top(input logic clk, aux, input logic [1:0] a, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] m [8];
  always_ff @(posedge clk) m[{1'b0, a}] <= d;
  always_ff @(posedge aux) y <= m[7];
endmodule
