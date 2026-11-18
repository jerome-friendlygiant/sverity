// sverity-args: --top top
// revert: assign y = a; => assign y = a; endmodule
// revert-expect-none: LNT-001
// The module body is never closed: slang reports the missing endmodule (unmapped, so LNT-001).
// expect: LNT-001 ERROR
// expect: LNT-001 FATAL
module top(input logic a, output logic y);
  assign y = a;
