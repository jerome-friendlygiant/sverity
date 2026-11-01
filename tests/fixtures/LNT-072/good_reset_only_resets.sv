// sverity-args: --top top
// the reset only resets registers; the clock is not derived from it
// expect-none: LNT-072
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
