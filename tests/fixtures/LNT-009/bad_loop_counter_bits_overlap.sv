// sverity-args: --top top
// revert: y[2] <= a => y[1] <= a
// revert-expect-none: LNT-009
// The loop writes bits 2 and 3 of y, and another process writes bit 2.
// expect: LNT-009 ERROR 9
module top(input wire clk, rst, input wire a, output reg [3:0] y);
  integer k;
  always @(posedge clk) y[2] <= a;
  always @(posedge clk) if (rst) for (k = 2; k < 4; k = k + 1) y[k] <= 1'b0;
endmodule
