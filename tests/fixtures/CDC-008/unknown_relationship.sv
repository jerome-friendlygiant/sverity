// sverity-args: --top top
// The source-side logic is known, but unconstrained clock intent keeps severity unresolved.
// expect: CDC-008 WARNING
// expect-unresolved: CDC-008
// expect-none: CDC-008 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, e, output logic q);
  logic a_q, b_q, s1, s2;
  always_ff @(posedge clka) begin a_q <= d; b_q <= e; end
  always_ff @(posedge clkb) begin s1 <= a_q & b_q; s2 <= s1; end
  assign q = s2;
endmodule
