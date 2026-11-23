// sverity-args: --top top
// A divided clock is recognized as a clock operand, but its phase relation is not inferred.
// The free select leaves the mux unresolved.
// expect: CDC-019 WARNING
// expect-unresolved: CDC-019
module top(input logic clka, sel, d, output logic q);
  logic cdiv;
  always_ff @(posedge clka) cdiv <= ~cdiv;
  wire c = sel ? cdiv : clka;
  always_ff @(posedge c) q <= d;
endmodule
