// sverity-args: --top top
// g is called only under a constant-false condition and f only by g, so the write f makes builds no hardware: x is never driven.
// expect: LNT-008 WARNING 5
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  function automatic void f(input logic [3:0] v); x = v; endfunction
  function automatic void g(input logic [3:0] v); f(v); endfunction
  always_comb if (1'b0) g(a);
  assign y = x;
endmodule
