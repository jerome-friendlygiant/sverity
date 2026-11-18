// sverity-args: --top top
// A package function of its arguments cannot have read `y`: the pair is decided, an advisory priority override (INFO), not Unresolved.
// expect: LNT-025 INFO 12
// expect-none: LNT-025 WARNING
package pk;
  function automatic logic both(logic a, logic b); return a & b; endfunction
endpackage
module top(input logic a, b, c, d, output logic y);
  always_comb begin
    y = 1'b0;
    if (a) y = b;
    if (pk::both(c, d)) y = d;
  end
endmodule
