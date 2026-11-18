// sverity-args: --top top
// expect-count: LNT-057 WARNING 2
module top #(parameter W=8, parameter type T=logic [7:0])(input logic [W-1:0] d, input logic [15:0] v, output logic [3:0] a,b,c);
  logic [$bits(d)-1:0] x;
  logic [$bits(T)-1:0] y;
  logic [$bits(v)-1:0] z;
  assign a=x;
  assign b=y;
  assign c=z;
endmodule
