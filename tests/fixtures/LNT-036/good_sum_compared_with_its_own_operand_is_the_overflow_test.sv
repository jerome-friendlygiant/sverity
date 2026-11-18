// sverity-args: --top top
// `(a + b) < a` is true exactly when the unsigned sum wrapped: the truncated sum is the point of the test.
// expect-none: LNT-036
module top(input logic [7:0] a, b, output logic ovf);
  assign ovf = (a + b) < a;
endmodule
