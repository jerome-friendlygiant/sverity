// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(input logic [3:0] a, input logic [W-1:0] b, output logic [3:0] q);
  assign q={a,b};
endmodule
