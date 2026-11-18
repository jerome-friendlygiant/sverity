// sverity-args: --top top
// The asynchronous reset is a single inversion of a port: glitch-free, INFO.
// expect: LNT-047 INFO 7
module top(input logic clk, rst, d, output logic q);
  logic rst_n;
  assign rst_n = ~rst;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
