// sverity-args: --top top
// expect: LNT-057 WARNING 5
// expect-count: LNT-057 WARNING 1
module top #(parameter W = 4) (input logic [7:0] a);
  wire [W-1:0] held = a;            // 8 bits into a W-bit net
endmodule
