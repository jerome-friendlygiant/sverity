// sverity-args: --top top
// An extension that keeps the value is INFO: a signed value sign-extended into a signed target (also through the arms of a conditional) and
// an unsigned value zero-extended into a signed or an unsigned one.
// expect: LNT-004 INFO 9
// expect: LNT-004 INFO 10
// expect: LNT-004 INFO 11
// expect-none: LNT-004 WARNING
module top(input logic s, input logic signed [7:0] a, b, input logic [7:0] u, output logic signed [15:0] p, q, r);
  assign p = s ? a : b;
  assign q = a;
  assign r = u;
endmodule
