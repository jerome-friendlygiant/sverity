// sverity-args: --top top
// Near-miss of the polarity mismatch: '~rst_n' on a 1-bit reset is the same test as '!rst_n'.
// expect-none: LNT-058
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (~rst_n) q <= 1'b0;
    else q <= d;
endmodule
