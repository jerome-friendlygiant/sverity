// sverity-args: --top top
// The select register runs on a third clock, so it changes at any moment relative to both muxed clocks.
// expect: LNT-047 WARNING 8
// expect-none: LNT-047 INFO
module top(input logic clk, clk_alt, clk_cfg, rst_n, request, d, output logic q);
  logic sel_q, core_clk;
  always_ff @(posedge clk_cfg or negedge rst_n) if (!rst_n) sel_q <= 1'b0; else sel_q <= request;
  assign core_clk = sel_q ? clk_alt : clk;
  always_ff @(posedge core_clk) q <= d;
endmodule
