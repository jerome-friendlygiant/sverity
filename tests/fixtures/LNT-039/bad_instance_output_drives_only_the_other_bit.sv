// sverity-args: --top top
// A child output drives t[1]; t[0] is only z and is the bit the second instance reads.
// expect: LNT-039 WARNING 9
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [1:0] t;
  assign t[0] = 1'bz;
  sub d(.a(x), .b(4'h0), .y(t[1]));
  sub u(.a(t[0]), .b(4'h0), .y(y));
endmodule
