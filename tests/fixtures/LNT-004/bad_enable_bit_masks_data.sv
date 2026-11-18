// sverity-args: --top top
// Lookalike of good_fill_literal_mask: a one-bit enable is zero-extended, so it masks bits 7:1 of the data.
// revert: & de => & {8{de}}
// revert-expect-none: LNT-004 WARNING
// expect: LNT-004 WARNING 7
module top(input logic de, input logic [7:0] d, output logic [7:0] y);
  assign y = d & de;
endmodule
