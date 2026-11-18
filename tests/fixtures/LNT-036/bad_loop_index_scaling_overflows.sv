// sverity-args: --top top
// A loop that runs to 2**30 times 8 leaves 32 bits, and a free 32-bit operand has no bound at all.
// expect: LNT-036 WARNING 8
// expect: LNT-036 WARNING 10
module top(input logic [63:0] a, input int unsigned n, output logic y, z);
  always_comb begin
    y = 1'b0;
    for (int i = 0; i < (1 << 30); i++) y ^= a[(i * 8) % 64];
  end
  assign z = a[(n * 8) % 64];
endmodule
