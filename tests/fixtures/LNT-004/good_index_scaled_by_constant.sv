// sverity-args: --top top
// A narrow unsigned index times a constant inside a part-select: the product lies in [0, 56], inside its 32-bit expression, so
// extending the narrower operand loses nothing. The finding is INFO with the value range.
// expect: LNT-004 INFO 8
// expect: LNT-004 INFO 9
// expect-none: LNT-004 WARNING
module top #(parameter int unsigned W = 8) (input logic [2:0] idx, input logic [63:0] data, output logic [W-1:0] y, z);
  assign y = data[idx * W +: W];
  assign z = data[8 * idx +: 8];
endmodule
