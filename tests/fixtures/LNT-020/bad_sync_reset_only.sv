// expect: LNT-020 WARNING 3
module top(input logic clk, rst, d, output logic q, r);
  always @(posedge clk) if (rst) q <= 0; else r <= d;
endmodule
