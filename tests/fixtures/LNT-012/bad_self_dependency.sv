// sverity-args: --top top
// expect: LNT-012 ERROR
module top(input logic a, output logic y);
  assign y = y ^ a;
endmodule
