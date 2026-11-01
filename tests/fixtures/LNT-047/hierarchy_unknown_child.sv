// sverity-args: --top top
// expect: LNT-047 WARNING 7
// expect-count: LNT-047 WARNING 2
module child(input logic clk, en, a, output logic q);
  logic gclk;
  assign gclk = clk & en;
  always @(posedge gclk) q <= a;
endmodule
module top(input logic clk, en, a, output logic [1:0] q);
  child u0(clk, en, a, q[0]);
  child u1(clk, en, a, q[1]);
endmodule
