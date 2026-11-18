// sverity-args: --top top
// Near-miss of bad_procedural_assign_to_net: the net w is driven only by a continuous assignment; the procedural
// assignment targets the variable v.
// expect-none: LNT-031
module top(input logic a, b, output logic y, output logic z);
  wire w;
  assign w = a;
  logic v;
  always_comb v = b;
  assign y = w;
  assign z = v;
endmodule
