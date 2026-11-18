// expect-none: LNT-067
module top(input logic clk, rst, d, output logic q);
  task update; q <= d; endtask
  always @(posedge clk) if (rst) q <= 0; else update();
endmodule
