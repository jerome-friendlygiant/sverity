// sverity-args: --top top
// Two inversions cancel: `out` is a copy of `in`.
// expect-none: LNT-047
module top(input logic in, d, output logic q);
  logic inv, out;
  assign inv = ~in;
  assign out = ~inv;
  always_ff @(posedge out) q <= d;
endmodule
