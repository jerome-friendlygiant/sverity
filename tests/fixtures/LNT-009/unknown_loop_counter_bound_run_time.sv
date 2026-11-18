// sverity-args: --top top
// The loop's bound is an input, so the bits y[k] reaches are not known: overlap with y[0] is not determinable.
// expect: LNT-009 WARNING 7
module top(input wire clk, input wire [2:0] n, input wire a, output reg [7:0] y);
  integer k;
  always @(posedge clk) y[0] <= a;
  always @(posedge clk) for (k = 0; k < n; k = k + 1) y[k] <= 1'b0;
endmodule
