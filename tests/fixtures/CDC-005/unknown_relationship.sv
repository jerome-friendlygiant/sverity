// sverity-args: --top top --option CDC-005.min_stages=3
// A two-stage chain is short, but without clock constraints its relationship is unknown.
// expect: CDC-005 WARNING
// expect-unresolved: CDC-005
// expect-none: CDC-005 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
endmodule
