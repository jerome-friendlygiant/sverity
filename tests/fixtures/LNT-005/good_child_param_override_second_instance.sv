// sverity-args: --top top
// The override is proven at its own site even when an earlier instance of the child has the same parameter values, so the
// slang body of this instance is shared with that one.
// expect: LNT-005 INFO 14
// expect-none: LNT-005 WARNING
module leaf #(parameter int W = 1) (input logic [W-1:0] d, output logic [W-1:0] q);
  assign q = d;
endmodule
module first(input logic [5:0] d, output logic [5:0] q);
  leaf #(.W(6)) u_leaf (.d(d), .q(q));
endmodule
module second #(parameter int Width = 3) (input logic [5:0] d, output logic [5:0] q);
  localparam int unsigned NumShares = 2;
  leaf #(.W(NumShares * Width)) u_leaf (.d(d), .q(q));
endmodule
module top(input logic [5:0] d, output logic [5:0] q, r);
  first u_first (.d(d), .q(q));
  second u_second (.d(d), .q(r));
endmodule
