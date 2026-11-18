// sverity-args: --top top
// The read of `a` feeds a write that a later write replaces, so the hardware is `a = b`: not a proven loop.
// expect-none: LNT-012 ERROR
module top(input logic [3:0] x, b, output logic [3:0] a);
  always_comb begin a = a ^ x; a = b; end
endmodule
