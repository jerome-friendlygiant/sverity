// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module child #(parameter W=4)(input logic [W-1:0] d[0:1]); endmodule
module top(input logic [7:0] a);
  child u(.d('{a, 4'b0}));
endmodule
