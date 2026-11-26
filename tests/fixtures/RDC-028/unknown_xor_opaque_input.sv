// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --ignore-unknown-modules
// The XOR with 1'b1 inverts a black-box output: whether the reset is the inverted copy of a source is not established.
// expect-unresolved: RDC-028
module top(input logic clka, d, output logic q);
  wire a; bb u(.o(a));
  wire rr = a ^ 1'b1;
  always_ff @(posedge clka or posedge rr) if (rr) q <= 1'b1; else q <= d;
endmodule
