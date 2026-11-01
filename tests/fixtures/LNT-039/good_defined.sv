// sverity-args: --top top
// expect-none: LNT-039
module top(input logic en, a, output logic y);
  assign y = en ? a : 1'b0;
endmodule
