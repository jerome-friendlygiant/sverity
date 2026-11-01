// sverity-args: --top top
// `t` is assigned and then re-assigned from itself inside ONE procedure: statement order makes this legal.
// The analysis is order-insensitive there, so at most a heuristic WARNING is allowed, never an ERROR.
// expect-none: LNT-012 ERROR
module top(input logic a, b, output logic y);
  logic t, u;
  always_comb begin
    t = a;
    u = t & b;
    t = u | a;
    y = t;
  end
endmodule
