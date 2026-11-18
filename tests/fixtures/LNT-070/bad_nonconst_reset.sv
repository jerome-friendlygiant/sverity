// sverity-args: --top top
// revert: if (!rst_n) q <= a; => if (!rst_n) q <= 1'b0;
// revert-expect-none: LNT-070 WARNING
// expect: LNT-070 WARNING 7
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= a;
    else q <= a;
endmodule
