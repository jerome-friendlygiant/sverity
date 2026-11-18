// expect-none: LNT-047
module clock_copy(input wire clk, output wire copied);
  assign copied = clk;
endmodule
module top(input logic clk, d, output logic q);
  wire copied;
  clock_copy u(clk, copied);
  always @(posedge copied) q <= d;
endmodule
