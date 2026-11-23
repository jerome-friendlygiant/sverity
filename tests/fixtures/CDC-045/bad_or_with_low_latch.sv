// sverity-args: --top top
// A low-phase latch does not make an OR/inverted-enable clock gate safe.
// expect: CDC-045 WARNING
// expect-none: CDC-045 INFO
module top(input logic clka, en_d, d, output logic q);
  logic en_q, en_l, g;
  always_ff @(posedge clka) en_q <= en_d;
  always_latch if (!clka) en_l = en_q;
  assign g = clka | ~en_l;
  always_ff @(posedge g) q <= d;
endmodule
