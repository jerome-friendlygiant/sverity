// sverity-args: --top top
// The event selects bit 1 of the vector, which is the gated clock.
// expect: LNT-047 WARNING 8
// expect-count: LNT-047 WARNING 1
module top(input logic clk, en, d, output logic q, r);
  wire [1:0] c;
  assign c[0] = clk;
  assign c[1] = clk & en;
  always @(posedge c[0]) q <= d;
  always @(posedge c[1]) r <= d;
endmodule
