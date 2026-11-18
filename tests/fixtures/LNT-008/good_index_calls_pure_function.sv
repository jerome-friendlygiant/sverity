// sverity-args: --top top
// The index calls an automatic function of its argument only: it is evaluated per loop value, so every bit is placed.
// expect-none: LNT-008
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  function automatic int ident(int x); return x; endfunction
  always_comb for (int i = 0; i < 8; i++) v[ident(i)] = c;
  assign y = v;
endmodule
