// sverity-args: --top top
// `t` is assigned and then re-assigned from itself inside ONE procedure: statement order makes this legal.
// Flow proves every read sees an earlier write, so this is no cycle at all.
// expect-none: LNT-012
module top(input logic a, b, output logic y);
  logic t, u;
  always_comb begin
    t = a;
    u = t & b;
    t = u | a;
    y = t;
  end
endmodule
