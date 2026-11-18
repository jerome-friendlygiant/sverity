// sverity-args: --top top
// The overflow-test exemption is for unsigned operands: a signed sum that wraps flips its sign, and a < test against an operand says nothing about it.
// expect: LNT-036 WARNING 5
module top(input logic signed [7:0] a, b, output logic ovf);
  assign ovf = (a + b) < a;
endmodule
