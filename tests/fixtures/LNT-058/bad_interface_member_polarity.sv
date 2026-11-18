// sverity-args: --top top
// expect: LNT-058 ERROR 5
interface rif; logic rst_n; endinterface
module sub(input logic clk, rif r, input logic d, output logic q);
  always_ff @(posedge clk or negedge r.rst_n) if (r.rst_n) q <= 1'b0; else q <= d;
endmodule
module top(input logic clk, rst_n, d, output logic q);
  rif r(); assign r.rst_n = rst_n;
  sub u(.clk, .r, .d, .q);
endmodule
