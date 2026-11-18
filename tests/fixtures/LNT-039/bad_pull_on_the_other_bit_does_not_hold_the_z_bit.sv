// sverity-args: --top top
// The pulldown holds bit 1; bit 0 is z with no pull and no other driver.
// expect: LNT-039 WARNING 9
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[0] = 1'bz;
  assign t[1] = x;
  pulldown (t[1]);
  sub u(.a(t[0]), .b(4'h0), .y(y));
endmodule
