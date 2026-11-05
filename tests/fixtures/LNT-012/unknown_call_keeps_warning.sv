// sverity-args: --top top
// A user call makes the procedure opaque: order is not proved, so the warning stays.
// expect: LNT-012 WARNING
module top(input logic a, output logic y);
  logic s;
  function automatic logic f(logic x); return x ^ s; endfunction
  always_comb begin
    s = a;
    s = f(s);
    y = s;
  end
endmodule
