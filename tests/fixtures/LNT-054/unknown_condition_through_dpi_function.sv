// sverity-args: --top top
// The condition calls a DPI function whose body cannot be examined: the registers it reads are unknown.
// expect: LNT-054 WARNING 8
module top(input logic clk, d, output logic y);
  import "DPI-C" function bit probe();
  logic q;
  always @(posedge clk) q <= d;
  always_comb if (probe()) y = d; else y = q;
endmodule
