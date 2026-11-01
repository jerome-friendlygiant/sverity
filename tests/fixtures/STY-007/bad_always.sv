// sverity-args: --top top
// expect: STY-007 INFO 5
// expect: STY-007 INFO 6
module top(input logic clk, input logic a, output logic y, output logic q);
  always @* y = a;
  always @(posedge clk) q <= a;
endmodule
