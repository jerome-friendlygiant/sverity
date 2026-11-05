// sverity-args: --top top
// Near-miss of bad_implicit_net: the wire is declared before it is assigned.
// expect-none: LNT-007
module top(input logic a, output logic y);
  wire wire_typo;
  assign wire_typo = a;
  assign y = wire_typo;
endmodule
