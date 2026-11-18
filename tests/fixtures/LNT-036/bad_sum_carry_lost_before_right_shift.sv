// sverity-args: --top top
// The average of two bytes: the sum is evaluated at 8 bits (the result is 8 bits), so the carry is dropped before the shift halves it.
// expect: LNT-036 WARNING 7
// revert: logic [7:0] y => logic [8:0] y
// revert-expect-none: LNT-036
module top(input logic [7:0] a, b, output logic [7:0] y);
  assign y = (a + b) >> 1;
endmodule
