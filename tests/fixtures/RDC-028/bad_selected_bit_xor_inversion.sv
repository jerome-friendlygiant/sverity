// sverity-args: --top top
// rr is bit 1 of a, the reset rst_n, XORed with a constant 1: an exact selected-bit inversion, the inverted copy of rst_n (INFO).
// expect: RDC-028 INFO
// expect-confidence: RDC-028 heuristic
module top(input wire clka, rst_n, por_rst_n, d, output logic q);
  wire [1:0] a = {rst_n, por_rst_n};
  wire rr = a[1] ^ 1'b1;
  always_ff @(posedge clka or posedge rr) if (rr) q <= 0; else q <= d;
endmodule
