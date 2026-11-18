// sverity-args: --top top
// expect-count: LNT-057 WARNING 2
`define WIDTH W
module top #(parameter W=8, parameter type T=logic [7:0])(input logic [15:0] a);
  logic [`WIDTH-1:0] x;
  T y;
  assign x=a;
  assign y=a;
endmodule
