// sverity-args: --top top
// u1 ties the condition to 0, but the else arm writes the variable too: every instance drives it whichever way the condition goes.
// expect-none: LNT-008
module child(input logic sel, input logic [7:0] a, b, output logic [7:0] q);
  logic [7:0] r;
  always_comb if (sel) r = a; else r = b;
  assign q = r;
endmodule
module top(input logic sel, input logic [7:0] a, b, output logic [7:0] q0, q1);
  child u0(.sel, .a, .b, .q(q0));
  child u1(.sel(1'b0), .a, .b, .q(q1));
endmodule
