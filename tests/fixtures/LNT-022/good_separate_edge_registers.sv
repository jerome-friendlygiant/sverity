// expect-none: LNT-022
module top(input logic clk, d, output logic q, r);
  always @(posedge clk) q <= d;
  always @(negedge clk) r <= d;
endmodule
