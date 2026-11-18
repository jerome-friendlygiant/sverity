// sverity-args: --top top
// expect: LNT-057 WARNING 4
module top #(parameter int W = 1)(output logic signed [W-1:0] q);
  assign q = 1;
endmodule
