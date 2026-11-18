// sverity-args: --top top
// `rst_n` resets `q`; `r` starts with a test of `rst_q`, which may be a registered reset or a flag: whether `r` has no reset is undecided.
// expect: LNT-068 WARNING 8
module top(input logic clk, rst_n, rst_q, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= a;
  always_ff @(posedge clk) if (rst_q) r <= 1'b0; else r <= a;
endmodule
