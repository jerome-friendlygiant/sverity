// sverity-args: --top top
// The index calls a function that keeps state between calls: it is not an affine index of the loop variable.
// expect: LNT-008 WARNING 5
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  function int f(int x); static int n = 0; n = n + 1; return x + n; endfunction
  always_comb for (int i = 0; i < 8; i++) v[f(i)] = c;
  assign y = v;
endmodule
