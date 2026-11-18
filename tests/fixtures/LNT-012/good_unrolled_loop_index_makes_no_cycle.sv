// sverity-args: --top top
// Reduced from OpenTitan i3c_core: only v[1] takes src, and src reads v[0], so there is no loop. The loop has a constant bound, is
// unrolled, and writes exactly v[1]: the absence of a cycle is proven (before the unrolling the computed index made the model reach
// every element and the result was Unresolved).
// expect-none: LNT-012
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
