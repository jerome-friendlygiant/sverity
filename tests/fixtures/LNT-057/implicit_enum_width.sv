// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(input logic [15:0] d);
  typedef enum int { FIRST, N=W, NEXT, FIX=5, FIXNEXT } sizes_t;
  logic [NEXT-1:0] x;
  logic [FIXNEXT-1:0] y;
  assign x=d;
  assign y=d;
endmodule
