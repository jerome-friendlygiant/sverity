// sverity-args: --top top
// The index read by both lanes of one write takes too many values to enumerate together, so the bits it reaches are a superset that meets the other writer only approximately.
// expect-unresolved: LNT-049 5
module top(input logic clk, input logic [31:0] s, input logic a, b, output logic [7:0] y);
  logic [1:0][1:0][1:0] m [2];
  always @(posedge clk) m[0][s][s][0] <= a;
  always @(posedge clk) m[0][0][1][0] <= b;
  assign y = m[0];
endmodule
