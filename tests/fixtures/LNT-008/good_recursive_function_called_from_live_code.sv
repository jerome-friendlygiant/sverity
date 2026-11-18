// sverity-args: --top top
// The same recursive function with a live call: x is driven.
// expect-none: LNT-008
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  function automatic void f(input logic [3:0] v, input int n); x = v; if (n > 0) f(v, n - 1); endfunction
  always_comb f(a, 2);
  assign y = x;
endmodule
