// sverity-args: --top top
// A top-level instance without ports is testbench-like: the statement is INFO, not WARNING.
// expect: LNT-079 INFO 7
// expect-none: LNT-079 WARNING
module top;
  logic clk = 0, d = 0, q;
  always @(posedge clk) force q = d;
endmodule
