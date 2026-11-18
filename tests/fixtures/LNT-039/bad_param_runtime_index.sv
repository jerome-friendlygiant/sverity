// sverity-args: --top top
// A non-constant index into a parameter with X bits may reach the X bit, so the select is reported.
// expect: LNT-039 WARNING 6
module top(input logic [1:0] i, output logic y);
  localparam logic [3:0] P = 4'b10x1;
  assign y = P[i];
endmodule
