// sverity-args: --top top
// The sign-bit extension is one bit too short for a 10-bit target: the concatenation has W+1 bits, not W+K for the target.
// expect: LNT-005 WARNING 5
module top(input logic signed [7:0] a, output logic signed [9:0] b);
  assign b = {a[7], a};
endmodule
