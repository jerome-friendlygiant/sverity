// sverity-args: --top top
// The event selects bit 0 of the vector, an unconditional copy of the clock; the gated bit 1 is not in its cone.
// expect-none: LNT-047
module top(input logic clk, en, d, output logic q);
  wire [1:0] c;
  assign c[0] = clk;
  assign c[1] = clk & en;
  always @(posedge c[0]) q <= d;
endmodule
