// sverity-args: --top top
// Each word holds two packed lanes: one process writes bit 0 of lane s, the other bit 0 of lane 1: with s = 1 both write the same bit of word 0.
// expect: LNT-049 WARNING 5
module top(input logic clk, s, input logic [3:0] a, b, output logic [3:0] y);
  logic [1:0][3:0] m [2];
  always_ff @(posedge clk) m[0][s][0] <= a[0];
  always_ff @(posedge clk) m[0][1][0] <= b[0];
  assign y = m[0][1];
endmodule
