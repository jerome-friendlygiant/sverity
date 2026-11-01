// sverity-args: --top top
// expect: LNT-039 WARNING 4
module top(input logic en, a, output logic y);
  assign y = en ? 'x : a;
endmodule
