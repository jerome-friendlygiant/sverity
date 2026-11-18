// sverity-args: --top top
// The loop unrolls: iteration i reads a[i+1], and a[1], a[2], a[3] are written only by later iterations or after the loop.
// expect: LNT-044 WARNING 7
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] a;
  always_comb begin
    for (int i = 0; i < 3; i++) a[i] = a[i+1] ^ x[i];
    a[3] = x[3];
  end
  assign y = a;
endmodule
