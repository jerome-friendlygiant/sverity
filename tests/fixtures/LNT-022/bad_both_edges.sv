// sverity-args: --top top
// expect: LNT-022 WARNING 4
module top(input logic clk, a, output logic q);
  always @(posedge clk or negedge clk) q <= a;
endmodule
