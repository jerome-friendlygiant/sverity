// sverity-args: --top top
// The shift is an operand of a concatenation, so it is evaluated at the 8 bits of `a`: a count of 8 or more (here up to 24) pushes
// the payload out of the byte before it is placed in the 32-bit word.
// expect: LNT-035 WARNING 8
// revert: a << {l, 3'b000} => a << l
// revert-expect-none: LNT-035
module top(input logic [7:0] a, input logic [1:0] l, output logic [31:0] y);
  assign y = {24'd0, a << {l, 3'b000}};
endmodule
