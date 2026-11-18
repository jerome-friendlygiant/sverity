// sverity-args: --top top
// Lookalike of good_child_param_override_second_instance, sharing the body of the first instance (W = 6 in both): the negative
// Width = -3 is turned into 4294967293 by the unsigned operand and the sum wraps back to 6.
// revert: NumShares * Width + 12 => 6
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 15
module leaf #(parameter int W = 1) (input logic [W-1:0] d, output logic [W-1:0] q);
  assign q = d;
endmodule
module first(input logic [5:0] d, output logic [5:0] q);
  leaf #(.W(6)) u_leaf (.d(d), .q(q));
endmodule
module second #(parameter int Width = -3) (input logic [5:0] d, output logic [5:0] q);
  localparam int unsigned NumShares = 2;
  leaf #(.W(NumShares * Width + 12)) u_leaf (.d(d), .q(q));
endmodule
module top(input logic [5:0] d, output logic [5:0] q, r);
  first u_first (.d(d), .q(q));
  second u_second (.d(d), .q(r));
endmodule
