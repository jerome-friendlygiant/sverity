// sverity-args: --top top
// Boundary: an inferred latch in an always_comb procedure is Comb, so CDC-001 excludes it.
// expect-none: CDC-001 WARNING
module top(input logic en, d, output logic q);
  always_comb if (en) q = d;
endmodule
