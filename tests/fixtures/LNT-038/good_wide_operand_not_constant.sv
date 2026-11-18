// sverity-args: --top top
// A 64-bit unsigned operand takes every value from 0 past 2**62, so a comparison with zero, a fill literal or a small constant
// is true for some values and false for others: no tautology, and nothing left undecided.
// expect-none: LNT-038
module top(input logic [63:0] w, output logic y0, y1, y2);
  assign y0 = (w != '0);
  assign y1 = (w == 5);
  assign y2 = (w > 100);
endmodule
