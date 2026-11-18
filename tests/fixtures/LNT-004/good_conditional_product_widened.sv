// sverity-args: --top top
// An arm of a conditional is evaluated at the width of its context: a * b is the full 64-bit product, as in the unconditional
// product on the next line, and fits 64 bits. The finding is INFO with the value range.
// expect: LNT-004 INFO 7
// expect-none: LNT-004 WARNING
module top(input logic s, input logic [31:0] a, b, output logic [63:0] y, z);
  assign y = s ? a * b : '0;
  assign z = a * b;
endmodule
