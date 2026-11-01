// sverity-args: --top top
// expect: LNT-020 WARNING 5
module top(input logic clk, rst_n, en, a, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (en) q <= a;
endmodule
