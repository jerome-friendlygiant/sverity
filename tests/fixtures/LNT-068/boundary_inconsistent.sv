// sverity-args: --top top
// expect: LNT-068 INFO 7
module top(input logic clk, rst_n, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= a;
  always_ff @(posedge clk) r <= a;
endmodule
// revert: always_ff @(posedge clk) r <= a; => always_ff @(posedge clk or negedge rst_n) if (!rst_n) r <= 0; else r <= a;
// revert-expect-none: LNT-068
