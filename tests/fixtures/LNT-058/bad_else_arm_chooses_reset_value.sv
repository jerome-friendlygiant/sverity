// sverity-args: --top top
// The else arm runs at the active level but picks its value with x: it is no constant reset value, so ERROR, not the else-branch WARNING.
// expect: LNT-058 ERROR 5
module top(input logic clk, rst_n, x, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (rst_n) q <= d;
    else if (x) q <= 1'b0;
    else q <= 1'b1;
endmodule
