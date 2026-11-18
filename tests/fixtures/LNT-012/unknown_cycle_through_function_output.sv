// sverity-args: --top top
// A function writes `a` through an output argument from its input `b`, and `b` reads `a`: the call may close a loop (Unresolved).
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic x, output logic y);
  logic a, b, r;
  function automatic logic f(output logic o, input logic i); o = ~i; return 1'b0; endfunction
  always_comb r = f(a, b);
  assign b = a & x;
  assign y = b | r;
endmodule
