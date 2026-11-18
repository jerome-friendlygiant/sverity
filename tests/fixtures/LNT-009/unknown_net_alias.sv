// sverity-args: --top top
// alias joins n and m, which are driven separately: the conflict is not analysed, so it is reported as Unresolved.
// expect: LNT-009 WARNING 6
module top(input logic a, b, output wire y);
  wire n, m;
  alias n = m;
  assign n = a;
  assign m = b;
  assign y = n;
endmodule
