// sverity-args: --top top
// expect: LNT-034 WARNING 5
// expect-count: LNT-034 WARNING 2
module top(input logic c, input logic [7:0] a, input logic [15:0] b, output logic [15:0] o, output logic [15:0] p);
  assign o = c ? a : b;          // 8-bit arm against 16-bit arm
  assign p = c ? a : 16'hFFFF;   // 8-bit arm against a sized literal
endmodule
