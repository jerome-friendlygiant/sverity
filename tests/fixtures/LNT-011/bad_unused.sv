// sverity-args: --top top
// expect: LNT-011 INFO
module top(input logic a, output logic y);
  logic spare;
  assign y = a;
endmodule
// revert: logic spare; => /* removed */
// revert-expect-none: LNT-011
