// sverity-args: --top top
// expect: LNT-007 WARNING
module top(input logic a, output logic y);
  assign wire_typo = a;
  assign y = wire_typo;
endmodule
