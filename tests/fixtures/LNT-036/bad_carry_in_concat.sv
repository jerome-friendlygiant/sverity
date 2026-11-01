// sverity-args: --top top
// expect: LNT-036 WARNING 5
// expect-count: LNT-036 WARNING 1
module top(input logic [7:0] a, b, output logic [15:0] w);
  assign w = {a + b};               // 8-bit sum inside a concatenation: carry lost
endmodule
