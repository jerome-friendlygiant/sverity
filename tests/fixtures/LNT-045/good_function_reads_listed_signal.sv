// sverity-args: --top top
// The function reads `b`, and `b` is in the list.
// expect-none: LNT-045
module top(input logic a, b, output logic y);
  function automatic logic f(logic v);
    return v & b;
  endfunction
  always @(a or b) y = f(a);
endmodule
