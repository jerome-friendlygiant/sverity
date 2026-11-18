// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(input logic [15:0] d);
  typedef enum int { N=W, FIX=8 } sizes_t;
  logic [N-1:0] x;
  logic [FIX-1:0] y;
  assign x=d;
  assign y=d;
endmodule
