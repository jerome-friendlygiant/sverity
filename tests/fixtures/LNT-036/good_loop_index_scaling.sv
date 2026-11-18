// sverity-args: --top top
// The operand ranges keep each product inside its 32 bits: a loop variable 0..15 times 8, a 4-bit unsigned times 2, and a
// remainder 0..3 times 8.
// expect-none: LNT-036
module top(input logic [127:0] a, input logic [3:0] n, input logic [31:0] s, output logic [127:0] y, output logic [7:0] z, m);
  always_comb begin
    for (int i = 0; i < 16; i++) y[8*i +: 8] = a[8*i +: 8];
    z = a[n * 2 +: 8];
    m = a[8 * ((7 - s) % 4) +: 8];
  end
endmodule
