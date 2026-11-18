// sverity-args: --top top
// Lookalike of the per-bit case: bit 0 has a second driver (an exclusive enable), so it is a bus, not a floating net.
// expect: LNT-039 INFO 8
// expect: LNT-039 INFO 9
// expect-none: LNT-039 WARNING
module top(input logic ea, eb, a, b, output logic y);
  wire [1:0] t;
  assign t[0] = ea ? a : 1'bz;
  assign t[0] = eb ? b : 1'bz;
  assign t[1] = 1'b0;
  assign y = t[0];
endmodule
