// sverity-args: --top top
// Bit 1 gets z from both part-selects and nothing else.
// expect: LNT-039 WARNING 9
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  wire [2:0] t;
  assign t[2:1] = {x, 1'bz};
  assign t[1:0] = 2'bzz;
  sub u(.a(t[1]), .b(4'h0), .y(y));
endmodule
