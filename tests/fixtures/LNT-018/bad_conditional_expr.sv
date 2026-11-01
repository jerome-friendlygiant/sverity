// sverity-args: --top top
// expect: LNT-018 WARNING 4
module top(input logic a, b, output logic y);
  assign y = (2 > 1) ? a : b;
endmodule
