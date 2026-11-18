// sverity-args: --top top
// expect-none: LNT-057
module top #(parameter int W = 1)(output logic [W-1:0] q);
  assign q = 1;
endmodule
