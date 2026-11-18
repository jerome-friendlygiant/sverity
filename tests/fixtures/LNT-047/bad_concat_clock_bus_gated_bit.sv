// sverity-args: --top top
// The event selects bit 0 of the bus, and the last operand of the concatenation is gated.
// expect: LNT-047 WARNING 6
module top(input logic clk_a, clk_b, en, d, output logic q);
  logic [1:0] clks;
  assign clks = {clk_b, clk_a & en};
  always_ff @(posedge clks[0]) q <= d;
endmodule
// revert: clk_a & en => clk_a
// revert-expect-none: LNT-047
