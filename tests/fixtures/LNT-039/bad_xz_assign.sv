// sverity-args: --top top
// expect: LNT-039 WARNING 4
module top(input logic a, output logic y);
  assign y = 'x;
endmodule
