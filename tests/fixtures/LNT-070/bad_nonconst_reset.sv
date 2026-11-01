// sverity-args: --top top
// expect: LNT-070 WARNING 5
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= a;
    else q <= a;
endmodule
