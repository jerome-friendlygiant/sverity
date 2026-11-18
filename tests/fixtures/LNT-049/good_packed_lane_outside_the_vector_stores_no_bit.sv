// sverity-args: --top top
// Lane 2 does not exist in a two-lane word: the write stores no bit, so the second process is the only writer of word 0 (slang or LNT-006 report the select itself).
// expect-none: LNT-049 WARNING
module top(input logic clk, input logic [3:0] a, b, output logic [3:0] y);
  logic [1:0][3:0] m [2];
  always_ff @(posedge clk) m[0][2][0] <= a[0];
  always_ff @(posedge clk) m[0][1][0] <= b[0];
  assign y = m[0][1];
endmodule
