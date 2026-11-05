// sverity-args: --top top
// Near-miss of bad_many_undeclared: every identifier is declared before it is used.
// expect-none: LNT-002
module top(input logic [1:0] a, output logic [1:0] y);
  logic t0, t1;
  assign t0 = a[0];
  assign t1 = a[1];
  assign y = {t1, t0};
endmodule
