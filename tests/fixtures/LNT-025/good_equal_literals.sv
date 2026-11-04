// sverity-args: --top top
// Equal destination values after conversion cannot conflict, even when both guards hold.
// expect-none: LNT-025
module top(input logic a, b, output logic [3:0] q);
  always_comb begin
    q = 4'd3;
    if (a) q = '0;
    if (b) q = 4'd0;
  end
endmodule
