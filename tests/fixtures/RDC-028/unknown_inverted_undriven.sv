// sverity-args: --top top
// The inversion's input is a net nothing drives: whether r is the inverted copy of rst_n is not established, so it is Unresolved, not an INFO copy.
// expect-unresolved: RDC-028
module top(input logic clka, d, rst_n, output logic q, p);
  wire bb;
  wire n = rst_n & bb;
  wire r = ~n;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or posedge r) if (r) p <= 1'b0; else p <= d;
endmodule
