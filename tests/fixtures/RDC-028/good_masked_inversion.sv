// sverity-args: --top top
// ~(rst_n & 1'b0) is the constant 1, not a copy of rst_n: no inverted-copy claim is made.
// expect-none: RDC-028
module top(input logic clka, d, rst_n, output logic q, p);
  wire r = ~(rst_n & 1'b0);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or posedge r) if (r) p <= 1'b0; else p <= d;
endmodule
