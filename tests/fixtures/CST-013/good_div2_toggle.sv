// sverity-args: --top top --sdc @DIR@/div2.sdc
// Exact toggle-flop divide-by-2 matches the declared create_generated_clock.
// expect-none: CST-013 WARNING
// expect-none: CST-013 ERROR
// expect: CST-013 INFO
module top(input logic clk_src, d, output logic q);
  logic div2_q;
  always_ff @(posedge clk_src) div2_q <= ~div2_q;
  always_ff @(posedge div2_q) q <= d;
endmodule
