// sverity-args: --top top
// A ported child of a portless top is design RTL, not testbench: its force is WARNING, even though the top is testbench-like.
// A port list makes an instance design RTL wherever it sits.
// expect: LNT-079 WARNING 9
// expect-count: LNT-079 WARNING 2
// expect-none: LNT-079 INFO
module dut(input logic clk, input logic a, output logic y);
  logic r;
  always @(posedge clk) if (a) force r = 1'b1; else release r;
  assign y = r;
endmodule
module top;
  logic clk, a, y;
  dut u(.clk(clk), .a(a), .y(y));
endmodule
