// sverity-args: --top top
// Near-miss of bad_divide_by_zero: the constant divisor is non-zero.
// expect-none: LNT-037
module top(input logic [7:0] a, output logic [7:0] y);
  assign y = a / 2;
endmodule
