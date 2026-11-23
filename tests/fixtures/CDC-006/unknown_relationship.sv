// sverity-args: --top top
// The inter-stage logic remains visible, while missing clock intent makes severity unresolved.
// expect: CDC-006 WARNING
// expect-unresolved: CDC-006
// expect-none: CDC-006 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= ~s1; end
  assign q = s2;
endmodule
