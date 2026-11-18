// sverity-args: --top top
// Lookalike of the unrolled feedback loop: each bit depends on the one before it and on an input, never on itself or a later bit.
// expect-none: LNT-012
module top(input logic [3:0] x, output logic [3:0] a);
  always_comb begin
    a[0] = x[0];
    for (int i = 1; i < 4; i++) a[i] = a[i-1] & x[i];
  end
endmodule
