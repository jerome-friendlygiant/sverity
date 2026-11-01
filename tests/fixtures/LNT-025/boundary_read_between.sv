// sverity-args: --top top
// the value written by the first if is read before the second one: a different pattern, not reported here
// expect-none: LNT-025
module top(input logic a, b, output logic y, w);
  logic x;
  always_comb begin
    x = 1'b0;
    if (a) x = 1'b1;
    w = x;
    if (b) x = 1'b0;
    y = x;
  end
endmodule
