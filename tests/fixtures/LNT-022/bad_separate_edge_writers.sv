// expect: LNT-022 WARNING 4
module top(input logic clk, d, output logic q);
  always @(posedge clk) q <= d;
  always @(negedge clk) q <= d;
endmodule
