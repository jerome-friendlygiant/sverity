// sverity-args: --top top
// The same run-time index selects both lanes of one write, so it reaches bits 0 or 5 of the word and never bit 2, which the other process writes.
// expect-none: LNT-049 WARNING
module top(input logic clk, s, input logic a, b, output logic [7:0] y);
  logic [1:0][1:0][1:0] m [2];
  always @(posedge clk) m[0][s][s][0] <= a;
  always @(posedge clk) m[0][0][1][0] <= b;
  assign y = m[0];
endmodule
