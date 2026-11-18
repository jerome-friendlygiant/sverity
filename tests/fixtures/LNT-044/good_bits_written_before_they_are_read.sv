// sverity-args: --top top
// Lookalike: the chain is written from the top bit down, so every bit is read after its write.
// expect-none: LNT-044
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] a;
  always_comb begin
    a[3] = x[3];
    for (int i = 2; i >= 0; i--) a[i] = a[i+1] ^ x[i];
  end
  assign y = a;
endmodule
