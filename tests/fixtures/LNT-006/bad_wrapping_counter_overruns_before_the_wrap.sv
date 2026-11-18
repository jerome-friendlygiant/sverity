// sverity-args: --top top
// A 3-bit counter against 7 never ends, but its first iterations run in order and read a[4] before it wraps.
// expect: LNT-006 ERROR 9
module top(input logic [3:0] c, output logic y);
  logic a [0:3];
  assign a[0] = c[0]; assign a[1] = c[1]; assign a[2] = c[2]; assign a[3] = c[3];
  always_comb begin
    y = 1'b0;
    for (bit [2:0] i = 0; i <= 7; i++) y = y ^ a[i];
  end
endmodule
