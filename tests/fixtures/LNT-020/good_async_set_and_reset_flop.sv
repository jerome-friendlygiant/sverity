// sverity-args: --top top
// A library flop with an asynchronous set and reset: every control's branch is a reset branch and the final else loads it.
// expect-none: LNT-020
// expect-none: LNT-067
module top(input logic clk, rst_n, set_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) q <= 1'b0;
    else if (!set_n) q <= 1'b1;
    else q <= d;
endmodule
