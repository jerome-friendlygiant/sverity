// sverity-args: --top top
// The function always reads `b`, which is not in the list: the block does not re-run when `b` changes, and the finding names `b`.
// expect: LNT-045 WARNING 10
// revert: always @(a) => always @(a or b)
// revert-expect-none: LNT-045
module top(input logic a, b, output logic y);
  function automatic logic f(logic v);
    return v & b;
  endfunction
  always @(a) y = f(a);
endmodule
