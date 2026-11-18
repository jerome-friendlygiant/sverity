// sverity-args: --top top
// One logic-driven clock for three register blocks is one finding, at the driver.
// expect: LNT-047 WARNING 7
// expect-count: LNT-047 WARNING 1
module top(input logic clk, clk_alt, sel, d, output logic q1, q2, q3);
  logic core_clk;
  assign core_clk = sel ? clk_alt : clk;
  always_ff @(posedge core_clk) q1 <= d;
  always_ff @(posedge core_clk) q2 <= d;
  always_ff @(posedge core_clk) q3 <= d;
endmodule
