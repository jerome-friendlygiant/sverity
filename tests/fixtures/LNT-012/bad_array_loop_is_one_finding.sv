// sverity-args: --top top
// A loop over an unpacked array closes at every bit of its elements: one finding for the cycle, not one per bit.
// expect-count: LNT-012 ERROR 1
module top(input logic [7:0] x, output logic [7:0] y);
  logic [7:0] m [2];
  always_comb m[0] = m[1] ^ x;
  always_comb m[1] = m[0];
  assign y = m[1];
endmodule
