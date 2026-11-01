// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A pure alias of a clock is the same clock: no generated-clock warning.
// expect-none: CDC-002
module top(input logic clka, rst_n, d, output logic q);
  logic clk_alias;
  assign clk_alias = clka;
  always_ff @(posedge clk_alias or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
