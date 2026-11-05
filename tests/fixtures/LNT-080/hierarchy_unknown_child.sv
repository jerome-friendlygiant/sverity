// sverity-args: --top top
// expect: LNT-080 WARNING 5
// expect-count: LNT-080 WARNING 2
module child(input logic clk, output logic q);
  real r;
  always_ff @(posedge clk) q <= 1'b0;
endmodule
module top(input logic clk, output logic [1:0] q);
  child u0(clk, q[0]);
  child u1(clk, q[1]);
endmodule
