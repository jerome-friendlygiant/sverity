// sverity-args: --top top
// The lane index of one writer is wider than the range proofs model, so the bits it reaches are only approximated: they meet the other writer's bit only in that approximation.
// expect-unresolved: LNT-049 5
module top(input logic clk, input logic [127:0] big, input logic [3:0] a, b, output logic [3:0] y);
  logic [1:0][3:0] m [2];
  always_ff @(posedge clk) m[0][big][0] <= a[0];
  always_ff @(posedge clk) m[0][1][0] <= b[0];
  assign y = m[0][1];
endmodule
