// sverity-args: --top top
// A ported child of a portless top is design RTL: its '#' delay is WARNING, not the testbench INFO.
// expect: LNT-019 WARNING 6
// expect-none: LNT-019 INFO
module dut(input logic clk, output logic y);
  always #1 y = clk;
endmodule
module top;
  logic clk = 0, y;
  dut u(.clk(clk), .y(y));
endmodule
