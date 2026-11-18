// sverity-args: --top top
// The wider operand is a signal that can hold any 32-bit value, so the product may wrap whatever the narrow index is.
// revert: input int stride => input logic [2:0] stride
// revert-expect-none: LNT-004
// expect: LNT-004 WARNING 7
module top(input logic [2:0] idx, input int stride, input logic [63:0] data, output logic [7:0] y);
  assign y = data[idx * stride +: 8];
endmodule
