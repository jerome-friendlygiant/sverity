// sverity-args: --top top
// Near-miss of bad_procedural_assign_to_net: w is a variable (logic), so the procedural assignment is legal.
// expect-none: LNT-031
module top(input logic a, output logic y);
  logic w;
  always_comb w = a;
  assign y = w;
endmodule
