// sverity-args: --top top --sdc @DIR@/explicit_master_source_mismatch.sdc
// expect: CST-013 ERROR
// expect-confidence: CST-013 established
// expect-coverage: constraints INCOMPLETE
module top(input logic clk_src, clk_other, d, output logic q);
  logic div2_q;
  always_ff @(posedge clk_other) div2_q <= ~div2_q;
  always_ff @(posedge div2_q) q <= d;
endmodule
