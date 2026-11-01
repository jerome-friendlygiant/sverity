// sverity-args: --top top
// revert: if (rst_n || x) => if (!rst_n || x)
// revert-expect-none: LNT-058 ERROR
// The compound first-if tests the active-low reset as if it were active-high: polarity mismatch.
// expect: LNT-058 ERROR
module top(input logic clk, rst_n, x, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (rst_n || x) q <= 1'b0; else q <= d;
endmodule
