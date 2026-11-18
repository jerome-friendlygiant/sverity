// sverity-args: --top top
// `t` is loaded in the final else and has no value in the reset or the set branch.
// expect: LNT-067 INFO 8
module top(input logic clk, rst_n, set_n, d, output logic q, t);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) q <= 1'b0;
    else if (!set_n) q <= 1'b1;
    else begin q <= d; t <= d; end
endmodule
