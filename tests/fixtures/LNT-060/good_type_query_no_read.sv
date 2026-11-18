// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output integer y);
  logic [1:0] q;
  always @(posedge clk) q[0] = 1;
  always_comb y = $bits(q) + $size(q);
endmodule
