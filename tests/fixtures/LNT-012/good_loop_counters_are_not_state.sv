// sverity-args: --top top
// A counter declared by the for header or inside the block is no hardware state, even when the loop is too long to be unrolled.
// expect-none: LNT-012
module top(input logic [15:0] a, output logic [15:0] y);
  always_comb begin
    int k;
    y = '0;
    for (int i = 0; i < 9000; i++) y[i % 16] = a[i % 16];
    for (k = 0; k < 9000; k++) y[k % 16] = a[k % 16];
  end
endmodule
