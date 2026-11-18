// sverity-args: --top top
// The sum is the least significant operand and a constant word at least as wide sits above it: a modular sum zero-extended, not a carry slot: INFO, not a warning.
// expect-none: LNT-036 WARNING
// expect: LNT-036 INFO 6
module top(input logic [31:0] a, b, output logic [63:0] w);
  assign w = {32'b0, a + b};
endmodule
