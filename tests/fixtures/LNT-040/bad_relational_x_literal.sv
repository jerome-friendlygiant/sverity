// sverity-args: --top top
// An X/Z literal in a relational comparison is LNT-040's, as for ==: the comparison is exempt from LNT-039.
// expect: LNT-040 WARNING 6
// expect-none: LNT-039
module top(input logic [1:0] a, output logic y);
  assign y = (a < 2'bx1);
endmodule
