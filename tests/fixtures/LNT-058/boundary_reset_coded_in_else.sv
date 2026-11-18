// sverity-args: --top top
// The reset values are in the else arm, which runs while rst_n is low: functionally right, so WARNING, not the ERROR of bad_polarity_neg.
// expect: LNT-058 WARNING 5
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (rst_n) q <= d;
    else q <= 1'b0;
endmodule
