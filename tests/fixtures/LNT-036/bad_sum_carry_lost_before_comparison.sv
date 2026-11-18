// sverity-args: --top top
// The sum of two bytes is compared with a byte: the comparison is evaluated at 8 bits, so the sum wraps before it is compared.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, c, output logic y);
  assign y = (a + b) > c;
endmodule
