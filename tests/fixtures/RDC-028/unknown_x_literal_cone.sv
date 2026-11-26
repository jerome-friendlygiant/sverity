// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The inverted reset is built through a net that combines the input with an X literal: the polarity of that copy is unknown.
// expect-unresolved: RDC-028
module top(input logic clka, rst_n, d, output logic q);
  wire n = rst_n & 1'bx;
  wire r = ~n;
  always_ff @(posedge clka or posedge r) if (r) q <= 1'b1; else q <= d;
endmodule
