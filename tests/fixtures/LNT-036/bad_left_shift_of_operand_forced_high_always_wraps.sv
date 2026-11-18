// sverity-args: --top top
// The operand's top bit is fixed to one, so shifting it left by two always pushes a one out of the byte.
// expect: LNT-036 WARNING 8
// expect-count: LNT-036 WARNING 1
module top(input logic [7:0] a, output logic [7:0] scaled);
  logic [7:0] upper_half;
  assign upper_half = {1'b1, a[6:0]};
  assign scaled = upper_half << 2;
endmodule
