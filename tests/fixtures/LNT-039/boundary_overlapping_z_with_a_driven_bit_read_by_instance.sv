// sverity-args: --top top
// Lookalike of the overlapping parts: the instance reads bit 1, which x drives.
// expect: LNT-039 INFO 10
// expect-none: LNT-039 WARNING
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[1:0] = 2'bzz;
  assign t[1] = x;
  sub u(.a(t[1]), .b(4'h0), .y(y));
endmodule
