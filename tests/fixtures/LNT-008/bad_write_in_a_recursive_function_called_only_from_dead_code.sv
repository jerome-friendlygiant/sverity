// sverity-args: --top top
// f calls itself and its only call from outside sits under a constant-false condition: the write builds no hardware, x is never driven.
// expect: LNT-008 WARNING 5
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  function automatic void f(input logic [3:0] v, input int n); x = v; if (n > 0) f(v, n - 1); endfunction
  always_comb if (1'b0) f(a, 2);
  assign y = x;
endmodule
