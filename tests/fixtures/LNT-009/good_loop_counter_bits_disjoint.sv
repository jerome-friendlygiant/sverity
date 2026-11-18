// sverity-args: --top top
// A module-level `integer` loop counter indexes y: the loop writes bits 2 and 3 only, not the whole vector (slang's driver
// for y[k] is all of y), so the write to bit 0 is not a second driver.
// expect-none: LNT-009
module top(input wire clk, rst, input wire a, output reg [3:0] y);
  integer k;
  always @(posedge clk) y[0] <= a;
  always @(posedge clk) if (rst) for (k = 2; k < 4; k = k + 1) y[k] <= 1'b0;
endmodule
