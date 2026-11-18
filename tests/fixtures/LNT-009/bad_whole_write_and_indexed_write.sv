// sverity-args: --top top
// One process writes every bit of y, the other one bit chosen at run time: whatever the index, that bit has two drivers.
// expect: LNT-009 ERROR 6
module top(input wire clk, input wire [1:0] i, input wire a, output reg [3:0] y);
  always @(posedge clk) y <= 4'b0;
  always @(posedge clk) y[i] <= a;
endmodule
