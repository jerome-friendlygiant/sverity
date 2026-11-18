// sverity-args: --top top
// expect-unresolved: LNT-060
// expect-coverage: rule-execution INCOMPLETE
module top(input logic clk);
  import "DPI-C" context task unknown_effect();
  always @(posedge clk) unknown_effect();
endmodule
