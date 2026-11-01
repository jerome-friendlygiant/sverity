// sverity-args: --top top
// the body calls a user function: signals read inside it cannot be checked, so the result is Unresolved
// expect: LNT-045 WARNING 8
module top(input logic a, output logic y);
  function automatic logic f(logic v);
    return ~v;
  endfunction
  always @(a) y = f(a);
endmodule
