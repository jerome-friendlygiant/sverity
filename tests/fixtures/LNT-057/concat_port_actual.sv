// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module child(input logic [3:0] d);
endmodule
module top #(parameter W=8)(input logic [3:0] a, input logic [W-1:0] b);
  child u(.d({a,b}));
endmodule
