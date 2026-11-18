// sverity-args: --top top
// An asynchronous reset for one net and a synchronous one for another.
// expect-none: LNT-084
module top(input logic clk, arst_n, srst_n, d, output logic q1, q2);
  always_ff @(posedge clk or negedge arst_n)
    if (!arst_n) q1 <= 1'b0;
    else q1 <= d;
  always_ff @(posedge clk)
    if (!srst_n) q2 <= 1'b0;
    else q2 <= d;
endmodule
