// sverity-args: --top top
// sverity-args: --profile fpga
// A time-zero initial block that leaves a constant in the whole register is the value FPGA configuration loads.
// expect-none: LNT-054
module top(input logic clk, d, output logic y);
  logic q;
  initial begin q = 0; end
  always @(posedge clk) q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
