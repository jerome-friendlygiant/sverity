// sverity-args: --top top
// expect: LNT-022 WARNING 4
module top(input logic clk1, clk2, a, output logic q);
  always @(posedge clk1 or posedge clk2) q <= a;
endmodule
