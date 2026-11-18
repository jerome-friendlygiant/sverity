// sverity-args: --top top
// The only call of f is under a constant-false condition, so the write it makes builds no hardware: x is never driven.
// expect: LNT-008 WARNING 5
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  function automatic void f(input logic [3:0] v); x = v; endfunction
  always_comb if (1'b0) f(a);
  assign y = x;
endmodule
