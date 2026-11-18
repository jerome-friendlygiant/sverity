// sverity-args: --top top
// Ascending and descending loops that stop exactly at the ends of the range.
// expect-none: LNT-006
module top(input logic [3:0] a, input logic [7:0] b, output logic [3:0] x, output logic [7:0] y);
  always_comb begin
    x = '0;
    for (int i = 0; i < 4; i++) x[i] = a[3 - i];
    y = '0;
    for (int j = 7; j >= 0; j--) y[j] = b[j];
  end
endmodule
