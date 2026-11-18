// sverity-args: --top top
// Lookalike of good_compare_parameter_arithmetic: `D - 8` is -2, which the unsigned comparison reads as 4294967294.
// revert: (D - 8) => (D - 2)
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 8
module top(input logic [2:0] l, output logic z);
  localparam int D = 6;
  assign z = l >= (D - 8);
endmodule
