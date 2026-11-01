// sverity-args: --top top
// expect: LNT-022 WARNING 5
// expect-count: LNT-022 WARNING 2
module child(input logic clk1, clk2, a, output logic q);
  always @(posedge clk1 or posedge clk2) q <= a;
endmodule
module top(input logic clk1, clk2, a, output logic [1:0] q);
  child u0(clk1, clk2, a, q[0]);
  child u1(clk1, clk2, a, q[1]);
endmodule
