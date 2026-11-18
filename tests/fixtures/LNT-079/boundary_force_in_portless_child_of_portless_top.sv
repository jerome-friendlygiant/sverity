// sverity-args: --top top
// A portless instance inside a portless top is testbench-like too (a bus-functional model with no ports): INFO, not WARNING.
// expect: LNT-079 INFO 7
// expect-none: LNT-079 WARNING
module bfm;
  logic clk = 0, d = 0, q;
  always @(posedge clk) force q = d;
endmodule
module top;
  bfm u_bfm();
endmodule
