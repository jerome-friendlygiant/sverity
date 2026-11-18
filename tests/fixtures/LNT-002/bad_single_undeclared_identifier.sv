// sverity-args: --top top
// The right-hand side names a signal that is never declared (slang UndeclaredIdentifier, mapped to LNT-002).
// expect: LNT-002 ERROR
module top(input logic a, output logic y);
  assign y = a & enable;
endmodule
