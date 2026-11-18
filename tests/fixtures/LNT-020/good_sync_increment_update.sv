// expect-none: LNT-020
module top(input logic clk, rst, en, output logic [3:0] count);
  always @(posedge clk) if (rst) count <= 0; else if (en) count++;
endmodule
