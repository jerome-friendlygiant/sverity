// sverity-args: --top top
// expect-none: LNT-036
module top(input logic [2:0] x, input logic signed [2:0] s, output logic [7:0] a,b,c);
  assign a = 8'd1 << (x-x);
  assign b = 8'd1 << (x+~x);
  assign c = 8'd1 << ((s & 3'sb001)+3'sd1);
endmodule
