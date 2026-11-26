// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Constant XOR polarity adapter (rst ^ 1'b1) is an exact inversion of rst: same leaf and effective polarity.
// expect-none: RDC-004
// expect-none: RDC-011
// expect-none: RDC-019
// expect-none: RDC-028 ERROR
module top(input logic clka, rst, d, output logic q);
  logic s;
  wire rst_n = rst ^ 1'b1;
  always_ff @(posedge clka or posedge rst) if (rst) s <= 1'b0; else s <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s;
endmodule
