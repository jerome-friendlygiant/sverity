// sverity-args: --top top
// The function reads nothing but its argument, which is in the list: the list is complete.
// expect-none: LNT-045
module top(input logic a, output logic y);
  function automatic logic f(logic v);
    return ~v;
  endfunction
  always @(a) y = f(a);
endmodule
