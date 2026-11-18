// sverity-args: --top top
// revert: logic msk; => logic mask;
// revert-expect-none: LNT-002
// The declaration is spelled msk but the use is mask: the use is undeclared (slang UndeclaredIdentifier, mapped to LNT-002).
// expect: LNT-002 ERROR
module top(input logic a, output logic y);
  logic msk;
  assign y = a & mask;
endmodule
