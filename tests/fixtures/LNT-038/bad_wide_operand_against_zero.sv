// sverity-args: --top top
// Unsigned is never below zero and always at least zero, however wide the operand, and never above all ones.
// revert: (w < 0) => (w < 1)
// revert: (w >= 0) => (w >= 1)
// revert: (w > '1) => (w > '0)
// revert-expect-none: LNT-038
// expect: LNT-038 WARNING 11
// expect: LNT-038 WARNING 12
// expect: LNT-038 WARNING 13
module top(input logic [63:0] w, output logic y0, y1, y2);
  assign y0 = (w >= 0);
  assign y1 = (w < 0);
  assign y2 = (w > '1);
endmodule
