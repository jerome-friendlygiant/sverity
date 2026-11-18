// sverity-args: --top top
// Each bit takes the bit above it, which is written one statement later: a[1], a[2] and a[3] are read at their previous values.
// expect: LNT-044 WARNING 7
module top(input logic [3:0] x, output logic [3:0] y);
  logic [3:0] a;
  always_comb begin
    a[0] = a[1] ^ x[0];
    a[1] = a[2] ^ x[1];
    a[2] = a[3] ^ x[2];
    a[3] = x[3];
  end
  assign y = a;
endmodule
