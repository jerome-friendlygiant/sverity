// sverity-args: --top top --sdc @DIR@/mux_bypass.sdc
// A clock mux/bypass path cannot be established as a fixed divide_by 2 by bounded RTL analysis.
// expect: CST-013 WARNING
// expect-unresolved: CST-013
// expect-coverage: constraints INCOMPLETE
module top(input logic clk_src, bypass, d, output logic q);
  logic div2_q, clk_out;
  always_ff @(posedge clk_src) div2_q <= ~div2_q;
  assign clk_out = bypass ? clk_src : div2_q;
  always_ff @(posedge clk_out) q <= d;
endmodule
