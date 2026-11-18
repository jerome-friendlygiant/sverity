// sverity-args: --top top
// A clock mux whose select is a register output: it changes synchronously, so the mux output switches between clocks only then.
// expect: LNT-047 INFO 8
// expect-none: LNT-047 WARNING
module top(input logic clk, clk_alt, rst_n, request, d, output logic q);
  logic sel_q, core_clk;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) sel_q <= 1'b0; else sel_q <= request;
  assign core_clk = sel_q ? clk_alt : clk;
  always_ff @(posedge core_clk) q <= d;
endmodule
