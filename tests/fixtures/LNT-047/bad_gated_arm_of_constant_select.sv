// sverity-args: --top top
// The constant select chooses the gated arm, so the clock net is logic.
// expect: LNT-047 WARNING 6
module top #(parameter bit Stub = 0)(input logic clk, en, d, output logic q);
  logic c;
  assign c = Stub ? clk : clk & en;
  always_ff @(posedge c) q <= d;
endmodule
