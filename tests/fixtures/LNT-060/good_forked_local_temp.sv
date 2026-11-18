// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, en);
  task automatic run; logic t; fork t = en; join_none endtask
  always @(posedge clk) run();
endmodule
