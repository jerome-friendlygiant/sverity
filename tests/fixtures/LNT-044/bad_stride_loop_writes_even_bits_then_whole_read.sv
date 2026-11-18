// sverity-args: --top top
// The loop writes the even bits, the whole vector is read, then the odd bits are written: the odd bits are read before they are written.
// expect: LNT-044 WARNING 8
module top(input logic [7:0] x, output logic [7:0] y);
  logic [7:0] a;
  always_comb begin
    for (int i = 0; i < 8; i += 2) a[i] = x[i];
    y = a;
    for (int i = 1; i < 8; i += 2) a[i] = x[i];
  end
endmodule
