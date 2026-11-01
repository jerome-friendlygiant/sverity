// sverity-args: --top top
// expect: LNT-009 ERROR
module top(input logic a, b, output logic y);
  assign y = a;
  assign y = b;
endmodule
