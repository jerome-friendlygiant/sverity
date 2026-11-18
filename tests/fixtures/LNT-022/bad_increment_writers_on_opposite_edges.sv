// sverity-args: --top top
// Unary updates are writes: a counter stepped on both edges of one clock.
// expect: LNT-022 WARNING 7
module top(input logic clk, output integer y);
  integer q;
  always @(posedge clk) q++;
  always @(negedge clk) q++;
  assign y = q;
endmodule
