// sverity-args: --top top
// Lookalike: a child output drives the z bit itself, so it is a bus.
// expect: LNT-039 INFO 10
// expect-none: LNT-039 WARNING
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[0] = 1'bz;
  assign t[1] = x;
  sub d(.a(x), .b(4'h0), .y(t[0]));
  sub u(.a(t[0]), .b(4'h0), .y(y));
endmodule
