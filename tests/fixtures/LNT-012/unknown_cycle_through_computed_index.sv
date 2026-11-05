// sverity-args: --top top
// Reduced from OpenTitan i3c_core: only v[1] takes src, and src reads v[0], so there is no loop, but the computed index makes
// the model reach every element. It cannot tell: Unresolved, never Established.
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic a, output logic [1:0] y);
  logic [1:0] v;
  logic src;
  always_comb begin
    v[0] = a;
    for (int unsigned t = 0; t < 1; t++) v[t + 1] = src;
  end
  assign src = v[0];
  assign y = v;
endmodule
