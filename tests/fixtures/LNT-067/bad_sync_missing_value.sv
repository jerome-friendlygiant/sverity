// expect: LNT-067 INFO 3
module top(input logic clk, rst, d, output logic q, r);
  always @(posedge clk) if (rst) r <= 0; else begin q <= d; r <= d; end
endmodule
