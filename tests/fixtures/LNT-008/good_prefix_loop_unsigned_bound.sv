// sverity-args: --top top
// Bit 0 is written alone, then a loop from 1 to an unsigned parameter writes v[i] from v[i-1].
// expect-none: LNT-008
module top #(parameter int unsigned N = 4) (input logic [N-1:0] a, output logic [N-1:0] y);
  logic [N-1:0] v;
  always_comb begin
    v[0] = a[0];
    for (int i = 1; i < N; i++) v[i] = v[i-1] | a[i];
  end
  assign y = v;
endmodule
