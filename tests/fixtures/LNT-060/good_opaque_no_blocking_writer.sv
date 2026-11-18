// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk);
  import "DPI-C" context task unknown_read();
  logic q;
  always @(posedge clk) q <= 1;
  always @* unknown_read();
endmodule
