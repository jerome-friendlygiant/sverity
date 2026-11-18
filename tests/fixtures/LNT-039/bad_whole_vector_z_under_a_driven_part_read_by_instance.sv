// sverity-args: --top top
// A whole-vector z, then a part driven by d: bit 0 is only ever z and the instance reads it.
// expect: LNT-039 WARNING 9
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire [1:0] d, output wire y);
  wire [3:0] t;
  assign t = 4'bzzzz;
  assign t[3:2] = d;
  sub u(.a(t[0]), .b(4'h0), .y(y));
endmodule
