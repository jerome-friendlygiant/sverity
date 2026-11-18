// sverity-args: --top top
// y[i+j] is a selector of two variables: the bits it reaches are not enumerated, so overlap with y[7] is neither proven nor excluded.
// expect: LNT-009 WARNING 6
module top(input wire clk, input wire [1:0] i, j, input wire a, b, output reg [7:0] y);
  always @(posedge clk) y[i+j] <= a;
  always @(posedge clk) y[7] <= b;
endmodule
