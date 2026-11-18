// sverity-args: --top top
// One process writes bit 0 of lane s (word bits 0 or 4), the other bit 2 of lane 0: the lanes between them are never touched by both.
// expect-none: LNT-049 WARNING
module top(input logic clk, s, input logic [3:0] a, b, output logic [3:0] y);
  logic [1:0][3:0] m [2];
  always_ff @(posedge clk) m[0][s][0] <= a[0];
  always_ff @(posedge clk) m[0][0][2] <= b[2];
  assign y = m[0][0];
endmodule
