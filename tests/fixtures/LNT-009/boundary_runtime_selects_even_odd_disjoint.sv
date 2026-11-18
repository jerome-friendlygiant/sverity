// sverity-args: --top top
// y[i*2] reaches even bits and y[j*2+1] odd bits: the bits each select can reach are disjoint (every value of the
// selector is tried), so the two writers never share a flip-flop.
// expect-none: LNT-009
module top(input wire clk, input wire [1:0] i, j, input wire a, b, output reg [7:0] y);
  always @(posedge clk) y[i*2] <= a;
  always @(posedge clk) y[j*2+1] <= b;
endmodule
