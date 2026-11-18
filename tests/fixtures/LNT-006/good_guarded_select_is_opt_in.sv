// sverity-args: --top top
// The guard keeps the index in range. Proving that is the opt-in's job (LNT-006.dynamic_index), so by default the
// select is not reported: only a select that runs on every iteration is decided without it.
// expect-none: LNT-006
module top(input logic [7:0] d, output logic [3:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 8; i++) if (i < 4) q[i] = d[i];
  end
endmodule
