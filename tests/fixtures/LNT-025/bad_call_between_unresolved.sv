// sverity-args: --top top
// a user call between two conditional writes cannot be proven harmless; reported as Unresolved, not hidden
// expect: LNT-025 WARNING
module top(input logic a, b, c, d, output logic y, z);
  function automatic logic inv(logic v); return ~v; endfunction
  always_comb begin
    y = 1'b0;
    if (c) y = a;
    z = inv(b);
    if (d) y = b;
  end
endmodule
