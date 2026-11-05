// sverity-args: --top top
// expect: LNT-080 WARNING 5
// expect: LNT-080 WARNING 6
module top(input logic clk, output logic q);
  real r;
  string s;
  always_ff @(posedge clk) q <= 1'b0;
endmodule
