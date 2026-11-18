// sverity-args: --top top
// expect-none: LNT-036
module top(input logic signed [7:0] a, b, output logic [9:0] y);
  assign y = {1'b0, $signed({a[7], a}) + $signed({b[7], b})};
endmodule
