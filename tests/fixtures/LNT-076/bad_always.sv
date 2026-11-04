// sverity-args: --top top
// expect: LNT-076 INFO 5
// expect: LNT-076 INFO 6
module top(input logic clk, input logic a, output logic y, output logic q);
  always @* y = a;
  always @(posedge clk) q <= a;
endmodule
