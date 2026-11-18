// sverity-args: --top top
// A user call reads `s` before the procedure has assigned it, and its result is assigned to `s`: whether that is a loop is not decided, so the warning stays.
// expect: LNT-012 WARNING
module top(input logic a, output logic y);
  logic s;
  function automatic logic f(logic x); return x ^ s; endfunction
  always_comb begin
    s = f(a);
    y = s;
  end
endmodule
