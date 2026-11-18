// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk);
  import "DPI-C" context task unknown_effect();
  always @(posedge clk) fork unknown_effect(); join_none
endmodule
