// sverity-args: --top top
// a signed difference, and a comparison of operands before subtracting, are fine
// expect-none: LNT-055
module top(input logic [7:0] a, b, output logic n1, n2);
  logic signed [8:0] d;
  assign d = $signed({1'b0, a}) - $signed({1'b0, b});
  assign n1 = d < 0;
  assign n2 = a < b;
endmodule
