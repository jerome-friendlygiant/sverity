// sverity-args: --top top
// Without clock declarations the crossing relationship cannot be established; retain an unresolved clock relation.
// expect-unresolved: CDC-028
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, clkb, d, en, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) if (en) q <= launch;
endmodule
