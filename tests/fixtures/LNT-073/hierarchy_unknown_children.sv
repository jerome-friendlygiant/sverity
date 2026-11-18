// sverity-args: --top top
// The constant 20 fits an 8-bit target (u2) but not a 4-bit one (u0, u1): reported per specialisation, grouped.
// expect: LNT-073 WARNING 6
// expect-count: LNT-073 WARNING 2
module child #(parameter W = 4) (output logic [W-1:0] o);
  assign o = 20;
endmodule
module top(output logic [3:0] a, b, output logic [7:0] c);
  child u0(.o(a));
  child u1(.o(b));
  child #(.W(8)) u2(.o(c));
endmodule
