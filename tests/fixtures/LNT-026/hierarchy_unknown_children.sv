// sverity-args: --top top
// Body W=4 is shared by u0 and u1, body W=8 is u2: one grouped record with count = instances (3).
// The 4'dx digits are undecidable here: Unresolved WARNING, reported at the literal.
// expect: LNT-026 WARNING 7
// expect-count: LNT-026 WARNING 3
module child #(parameter W = 4) (output logic [W-1:0] o);
  assign o = 4'dx;
endmodule
module top(output logic [3:0] a, b, output logic [7:0] c);
  child u0(.o(a));
  child u1(.o(b));
  child #(.W(8)) u2(.o(c));
endmodule
