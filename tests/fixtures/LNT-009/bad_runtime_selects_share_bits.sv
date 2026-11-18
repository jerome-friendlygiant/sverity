// sverity-args: --top top
// Two processes write bits of the packed vector y chosen at run time; both indexes reach bits 0 to 3, which are flip-flops in each.
// expect: LNT-009 ERROR 6
module top(input wire clk, input wire [1:0] i, j, input wire a, b, output reg [3:0] y);
  always @(posedge clk) y[i] <= a;
  always @(posedge clk) y[j] <= b;
endmodule
