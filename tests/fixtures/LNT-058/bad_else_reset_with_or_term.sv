// sverity-args: --top top
// With `rst_n || x` the else arm is skipped while x is high, although rst_n is low: the reset is lost. Not the else-branch WARNING, which needs a bare test.
// expect: LNT-058 ERROR 5
module top(input logic clk, rst_n, x, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (rst_n || x) q <= d;
    else q <= 1'b0;
endmodule
