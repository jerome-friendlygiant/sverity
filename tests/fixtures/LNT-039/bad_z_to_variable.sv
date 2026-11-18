// sverity-args: --top top
// 'z to a variable is not a tri-state: the INFO exemption applies to nets only.
// expect: LNT-039 WARNING 6
// expect-none: LNT-039 INFO
module top(input logic en, a, output logic y);
  always_comb y = en ? a : 'z;
endmodule
