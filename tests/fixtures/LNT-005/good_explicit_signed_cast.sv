// sverity-args: --top top
// Near-miss of bad_sign_compare: the unsigned operand is explicitly cast to signed, which LNT-005 exempts.
// expect-none: LNT-005
module top(input logic signed [3:0] s, input logic [3:0] u, output logic r);
  assign r = s < signed'(u);
endmodule
