// sverity-args: --top top
// `rst_n` is in the event list of the first block and tested without it in the second.
// expect: LNT-084 WARNING 8
module top(input logic clk, rst_n, d, output logic q1, q2);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q1 <= 1'b0;
    else q1 <= d;
  always_ff @(posedge clk)
    if (!rst_n) q2 <= 1'b0;
    else q2 <= d;
endmodule
