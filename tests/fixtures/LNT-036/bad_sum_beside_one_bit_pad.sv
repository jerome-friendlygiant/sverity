// sverity-args: --top top
// Lookalike of good_sum_zero_extended_into_wider_word: a one-bit pad is a carry slot the 8-bit sum cannot fill.
// revert: {1'b0, a + b} => {1'b0, {1'b0, a} + {1'b0, b}}
// revert-expect-none: LNT-036 WARNING
// expect: LNT-036 WARNING 7
module top(input logic [7:0] a, b, output logic [8:0] w);
  assign w = {1'b0, a + b};
endmodule
