// sverity-args: --top top
// x &= 0 replaces the old value with zero, so the earlier self term is not carried: no proven loop, only the ordered-read warning.
// expect: LNT-012 WARNING 6
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  always_comb begin x = x ^ a; x &= 4'h0; end
  assign y = x;
endmodule
