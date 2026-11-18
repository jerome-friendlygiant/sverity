// sverity-args: --top top
// A pullup primitive is resolved by strength and is exempt.
// expect-none: LNT-009
module top(input logic a, output wire w);
  pullup (w);
  assign w = a;
endmodule
