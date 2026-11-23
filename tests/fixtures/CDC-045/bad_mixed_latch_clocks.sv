// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-045 WARNING
// expect-none: CDC-045 INFO
module top(input logic clka, clkb, en_a, en_b, d, output logic q);
  logic allow_a, allow_b;
  always_latch if (!clka) allow_a <= en_a;
  always_latch if (!clkb) allow_b <= en_b;
  wire gclk = clka & allow_a & allow_b;
  always_ff @(posedge gclk) q <= d;
endmodule
