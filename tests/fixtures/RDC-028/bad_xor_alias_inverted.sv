// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The XOR with 1'b1 inverts an exact copy of the input (wire a = rst_n): the inverted copy is found through the alias.
// revert: wire rr = a ^ 1'b1; => wire rr = a;
// revert-expect-none: RDC-028 INFO
// expect: RDC-028 INFO
module top(input logic clka, rst_n, d, output logic q);
  wire a = rst_n;
  wire rr = a ^ 1'b1;
  always_ff @(posedge clka or posedge rr) if (rr) q <= 1'b1; else q <= d;
endmodule
