// sverity-args: --top top
// A ported child of a portless top is design RTL: a real variable in it is WARNING, not the testbench INFO.
// expect: LNT-080 WARNING 6
// expect-none: LNT-080 INFO
module dut(input logic clk, output logic y);
  real r;
  assign y = clk;
endmodule
module top;
  logic clk = 0, y;
  dut u(.clk(clk), .y(y));
endmodule
