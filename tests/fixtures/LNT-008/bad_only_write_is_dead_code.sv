// sverity-args: --top top
// The only write to `a` sits under a constant-false condition: it builds no hardware, so `a` is never driven although the source
// assigns it (slang's own "never assigned" does not fire).
// expect: LNT-008 WARNING 6
module top(input logic c, output logic y);
  logic a [0:3];
  always_comb if (1'b0) a[0] = c;
  assign y = a[0];
endmodule
