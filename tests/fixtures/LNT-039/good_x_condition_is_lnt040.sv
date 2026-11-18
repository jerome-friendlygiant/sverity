// sverity-args: --top top
// An X selecting a conditional is not a value written: LNT-040 reports it, LNT-039 does not.
// expect-none: LNT-039
// expect: LNT-040 WARNING 6
module top(input logic a, input logic b, output logic y);
  assign y = 1'bx ? a : b;
endmodule
