// sverity-args: --top top
// expect: LNT-036 WARNING 5
// expect-count: LNT-036 WARNING 1
module top(input logic [7:0] a, b, output logic [7:0] p, output logic [15:0] q);
  assign p = a * b;                 // 16-bit product evaluated at 8 bits
  assign q = a * b;                 // 16-bit target: fits
endmodule
