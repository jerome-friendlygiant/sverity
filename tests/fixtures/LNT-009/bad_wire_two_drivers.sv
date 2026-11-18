// sverity-args: --top top
// revert: assign w = b; => assign w = 1'bz;
// revert-expect-none: LNT-009 ERROR
// Two continuous drivers on a plain wire, neither can release it: a driver conflict that slang does not report for nets.
// expect: LNT-009 ERROR
module top(input logic a, b, output wire w);
  assign w = a;
  assign w = b;
endmodule
