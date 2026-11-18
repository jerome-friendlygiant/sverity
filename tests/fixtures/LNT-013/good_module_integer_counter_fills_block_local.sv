// sverity-args: --top top
// A module-level integer counter in a combinational block: the loop has constant bounds and writes every bit of the block local.
// expect-none: LNT-013
module top(input [7:0] a, output reg [7:0] y);
  integer i;
  always @* begin : b1
    reg [7:0] t;
    for (i = 0; i < 8; i = i + 1) t[i] = a[7-i];
    y = t;
  end
endmodule
