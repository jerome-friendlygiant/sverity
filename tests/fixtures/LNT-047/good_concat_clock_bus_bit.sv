// sverity-args: --top top
// Bit 0 of a bus built by concatenation is the last operand, a plain copy of clk_a.
// expect-none: LNT-047
module top(input logic clk_a, clk_b, d, output logic q);
  logic [1:0] clks;
  assign clks = {clk_b, clk_a};
  always_ff @(posedge clks[0]) q <= d;
endmodule
