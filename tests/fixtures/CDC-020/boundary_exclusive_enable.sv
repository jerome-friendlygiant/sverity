// sverity-args: --top top --sdc @DIR@/exclusive.sdc
// Boundary: a known Exclusive enable domain is not Same, so CDC-020 reports INFO rather than suppressing it.
// expect: CDC-020 INFO
// expect-confidence: CDC-020 established
module top(input logic clka, clkb, en_d, d, output logic q);
  logic en_q, gclk;
  always_ff @(posedge clkb) en_q <= en_d;
  assign gclk = clka & en_q;
  always_ff @(posedge gclk) q <= d;
endmodule
