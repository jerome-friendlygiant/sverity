// sverity-args: --top top
// The same shape, but the loop can leave early on a run-time value, so it is not unrolled and the computed index makes the model reach
// every element. It cannot tell: Unresolved, never Established.
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic a, output logic [1:0] y);
  logic [1:0] v;
  logic src;
  always_comb begin
    v[0] = a;
    for (int unsigned t = 0; t < 1; t++) begin v[t + 1] = src; if (a) break; end
  end
  assign src = v[0];
  assign y = v;
endmodule
