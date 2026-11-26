// sverity-args: --top top
// rst is the reset rst_n through a constant XOR: a polarity adapter. Its only uses are asynchronous reset pins, so the reset is
// routed, not used as functional data (RDC-019 must stay silent).
// expect-none: RDC-019
module top(input logic clka, rst_n, d, output logic p, q);
  wire rst = rst_n ^ 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
  always_ff @(posedge clka or posedge rst) if (rst) p <= 0; else p <= d;
endmodule
