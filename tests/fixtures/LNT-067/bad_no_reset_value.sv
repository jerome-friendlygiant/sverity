// sverity-args: --top top
// r is loaded in the normal branch but has no value in the reset branch
// expect: LNT-067 INFO 7
module top(input logic clk, rst_n, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else begin q <= a; r <= a; end
endmodule
// revert: q <= 1'b0; => begin q <= 1'b0; r <= 1'b0; end
// revert-expect-none: LNT-067
