// sverity-args: --top top
// the reset branch of a block with several asynchronous controls assigns a non-constant value
// expect: LNT-070 WARNING
module top(input logic clk, rst_n, set_n, d, d2, output logic q);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) q <= d2; else if (!set_n) q <= 1'b1; else q <= d;
endmodule
