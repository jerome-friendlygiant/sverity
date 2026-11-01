// sverity-args: --top top
// expect-none: LNT-022
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= a;
endmodule
