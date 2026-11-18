// sverity-args: --top top
// A constant user function of a parameter (`f(W)`) is evaluated for each specialisation like `$clog2(W)`: the width of `x` has parameter
// provenance, so the truncation is parameter-dependent (heuristic), no longer undecided.
// expect: LNT-057 WARNING 13
// expect: LNT-057 WARNING 14
// expect-confidence: LNT-057 heuristic 13
// expect-confidence: LNT-057 heuristic 14
module top #(parameter W=8)(input logic [15:0] a);
  function automatic integer f(input integer x); return 8; endfunction
  localparam N=f(W);
  logic [N-1:0] x;
  logic [W-1:0] y;
  assign x=a;
  assign y=a;
endmodule
