// sverity-args: --top top
// The function reads `b` only when `v` is set: whether the block misses a change of `b` depends on the call, so the result stays Unresolved.
// expect-unresolved: LNT-045 9
module top(input logic a, b, output logic y);
  function automatic logic f(logic v);
    if (v) return b;
    return 1'b0;
  endfunction
  always @(a) y = f(a);
endmodule
