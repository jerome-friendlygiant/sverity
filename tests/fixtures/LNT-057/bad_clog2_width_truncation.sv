// sverity-args: --top top
// The target's width is `$clog2(N)`, a constant function of the parameter N: the truncation is parameter-dependent, not undecided.
// expect: LNT-057 WARNING 6
// expect-confidence: LNT-057 heuristic 6
module top #(parameter int N = 8) (input logic [7:0] x, output logic [$clog2(N)-1:0] idx);
  assign idx = x;
endmodule
