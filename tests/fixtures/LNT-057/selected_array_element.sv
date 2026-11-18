// sverity-args: --top top
// expect-count: LNT-057 WARNING 2
module top #(parameter W=8, N=2)(input logic [W-1:0] a[N], input logic [7:0] b[N], input logic [N-1:0][W-1:0] c, input logic [N-1:0][7:0] d, output logic [3:0] p,q,r,s);
  assign p=a[0];
  assign q=b[0];
  assign r=c[0];
  assign s=d[0];
endmodule
