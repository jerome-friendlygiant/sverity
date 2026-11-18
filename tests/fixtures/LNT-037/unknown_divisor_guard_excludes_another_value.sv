// sverity-args: --top top
// Lookalike of good_divisor_guarded_by_zero_test: the test removes the value one, not zero, so zero still reaches the division.
// expect: LNT-037 WARNING 7
module top(input logic [7:0] n, d, output logic [7:0] q);
  always_comb begin
    if (d == 8'd1) q = n;
    else           q = n / d;
  end
endmodule
