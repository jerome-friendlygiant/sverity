// sverity-args: --top top
// Lookalike: the writer's index ranges over 0..7, which includes the word the other clock reads.
// revert: always_ff @(posedge aux) => always_ff @(posedge clk)
// revert-expect-none: LNT-082 WARNING
// expect-count: LNT-082 WARNING 2
module top(input logic clk, aux, input logic [2:0] a, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] m [8];
  always_ff @(posedge clk) m[a] <= d;
  always_ff @(posedge aux) y <= m[7];
endmodule
