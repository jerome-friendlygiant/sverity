// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk);
  import "DPI-C" context task unknown_read();
  logic q;
  always @(posedge clk) q = 1;
  always_comb unknown_read();
endmodule
