// sverity-args: --top top
// expect: LNT-036 WARNING 5
// expect-count: LNT-036 WARNING 1
module top(input logic signed [7:0] a, b, output logic [9:0] y);
  assign y = {1'b0, $signed({a[6], a}) + $signed({b[6], b})};
endmodule
