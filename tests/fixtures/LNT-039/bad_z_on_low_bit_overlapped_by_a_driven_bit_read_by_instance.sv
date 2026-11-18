// sverity-args: --top top
// The part-selects overlap: bit 1 is driven by x as well as z, bit 0 only by z. The instance reads bit 0, which floats.
// expect: LNT-039 WARNING 9
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[1:0] = 2'bzz;
  assign t[1] = x;
  sub u(.a(t[0]), .b(4'h0), .y(y));
endmodule
