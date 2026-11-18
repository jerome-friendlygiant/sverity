// sverity-args: --top top
// revert: (a || !a) => (a || b)
// revert-expect-none: LNT-038
// a || !a is true for every known value
// expect: LNT-038 WARNING 7
module top(input logic a, input logic b, output logic y);
  assign y = b & (a || !a);
endmodule
