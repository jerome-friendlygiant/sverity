// sverity-args: --top top
// a || !a is true for every known value
// expect: LNT-038 WARNING 5
module top(input logic a, input logic b, output logic y);
  assign y = b & (a || !a);
endmodule
