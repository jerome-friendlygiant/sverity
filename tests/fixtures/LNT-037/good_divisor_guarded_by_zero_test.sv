// sverity-args: --top top
// The division runs only where the divisor is not zero, in an if/else and in a conditional operator.
// expect-none: LNT-037
module top(input logic [7:0] n, d, output logic [7:0] q, r);
  always_comb begin
    if (d == 8'd0) q = 8'hff;
    else           q = n / d;
  end
  assign r = (d != 8'd0) ? n % d : 8'd0;
endmodule
