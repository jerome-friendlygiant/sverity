// sverity-args: --top top
// expect: LNT-058 WARNING 4
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n) q <= a;
endmodule
