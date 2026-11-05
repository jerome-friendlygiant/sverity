// sverity-args: --top top
// a -> b -> a is a real loop with no call in it. The separate branch through f must not lower it.
// expect: LNT-012 ERROR
module top(output wire a, b, c);
  function automatic logic f(input logic x); return x; endfunction
  assign a = b ^ c;
  assign b = ~a;
  assign c = f(a);
endmodule
