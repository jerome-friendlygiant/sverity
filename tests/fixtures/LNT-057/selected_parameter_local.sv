// sverity-args: --top top
// expect-count: LNT-057 WARNING 2
module top #(parameter [3:0] B=3)(input logic [15:0] a);
  localparam [3:0] L=3;
  localparam N=B[0]+7, M=N, FIX=L[0]+7;
  logic [N-1:0] x;
  logic [M-1:0] y;
  logic [FIX-1:0] z;
  assign x=a;
  assign y=a;
  assign z=a;
endmodule
