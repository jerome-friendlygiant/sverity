// sverity-args: --top top
// expect-unresolved: LNT-057 4
module top #(parameter W = 8) (input logic [W-1:0] a, output logic [3:0] q);
  assign q = signed'(a);
endmodule
