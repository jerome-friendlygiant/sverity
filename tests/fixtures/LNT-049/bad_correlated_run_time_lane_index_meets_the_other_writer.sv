// sverity-args: --top top
// The same run-time index selects both lanes of one write, so with s = 1 it writes bit 3 of the word, the bit the other process writes.
// expect: LNT-049 WARNING 5
module top(input logic clk, s, input logic a, b, output logic [7:0] y);
  logic [1:0][1:0][1:0] m [2];
  always @(posedge clk) m[0][s][s][0] <= a;
  always @(posedge clk) m[0][1][1][0] <= b;
  assign y = m[0];
endmodule
