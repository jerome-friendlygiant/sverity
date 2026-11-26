// sverity-args: --top top
// revert: wire r = rst_n ^ 1'b1; => wire r = rst_n ^ d;
// revert-expect-none: RDC-028 INFO
// x ^ 1'b1 is the exact inversion of x, the same as ~x: an inverted copy (INFO).
// expect: RDC-028 INFO
module top(input logic clka, d, rst_n, output logic q, p);
  wire r = rst_n ^ 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or posedge r) if (r) p <= 1'b0; else p <= d;
endmodule
