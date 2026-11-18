// sverity-args: --top top
// 8'(a) is a in the same width: (a + b) < 8'(a) is still the unsigned overflow test.
// expect-none: LNT-036
module top(input logic [7:0] a, b, output logic ovf);
  assign ovf = (a + b) < 8'(a);
endmodule
