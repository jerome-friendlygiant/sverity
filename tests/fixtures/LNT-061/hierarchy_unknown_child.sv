// sverity-args: --top top
// expect: LNT-061 WARNING 5
// expect-count: LNT-061 WARNING 2
module child(input logic clk, input logic a, output logic q);
  always @(posedge clk) $display("a=%b", a);
  assign q = a;
endmodule
module top(input logic clk, input logic a, output logic [1:0] q);
  child u0(clk, a, q[0]);
  child u1(clk, a, q[1]);
endmodule
