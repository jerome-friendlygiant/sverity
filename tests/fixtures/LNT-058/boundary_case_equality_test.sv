// sverity-args: --top top
// === and !== against 0 or 1 read like == and !=.
// expect-none: LNT-058
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (rst_n === 1'b0) q <= 1'b0; else q <= d;
endmodule
