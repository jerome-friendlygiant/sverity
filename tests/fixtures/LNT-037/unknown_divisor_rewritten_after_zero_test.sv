// sverity-args: --top top
// Lookalike of good_divisor_guarded_by_zero_test: the block writes the divisor after the test, so the tested value is not the one divided by.
// expect: LNT-037 WARNING 10
module top(input logic [7:0] n, d_in, output logic [7:0] q);
  logic [7:0] d;
  always_comb begin
    d = d_in;
    if (d != 8'd0) begin
      d = d_in >> 1;
      q = n / d;
    end else q = 8'hff;
  end
endmodule
