// sverity-args: --top top
// Two 40-bit operands need 80 bits for their product, more than the 64-bit context: the product wraps, so the arm is not proven.
// expect: LNT-004 WARNING 5
module top(input logic s, input logic [39:0] a, b, output logic [63:0] y);
  assign y = s ? a * b : '0;
endmodule
