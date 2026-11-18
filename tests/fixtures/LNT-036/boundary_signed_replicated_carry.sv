// sverity-args: --top top
// Ascending declaration: its leftmost index is the sign bit.
// expect-none: LNT-036
module top(input logic signed [0:7] a, b, output logic [10:0] y);
  assign y = {1'b0, $signed({{2{a[0]}}, a}) + $signed({{2{b[0]}}, b})};
endmodule
