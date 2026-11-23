// sverity-args: --top top --sdc @DIR@/source_mismatch.sdc
// Generated clock declares -source clk_src, but div2_q is actually clocked by clk_other.
// expect: CST-013 ERROR
// expect-coverage: constraints INCOMPLETE
// revert: always_ff @(posedge clk_other) div2_q <= ~div2_q; => always_ff @(posedge clk_src) div2_q <= ~div2_q;
// revert-expect-none: CST-013 ERROR
module top(input logic clk_src, clk_other, d, output logic q);
  logic div2_q;
  always_ff @(posedge clk_other) div2_q <= ~div2_q;
  always_ff @(posedge div2_q) q <= d;
endmodule
