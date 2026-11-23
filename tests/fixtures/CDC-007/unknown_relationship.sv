// sverity-args: --top top
// Early-stage fanout is established structurally, but the crossing relation is unknown.
// expect: CDC-007 WARNING
// expect-unresolved: CDC-007
// expect-none: CDC-007 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q, tap);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
  assign tap = s1;
endmodule
