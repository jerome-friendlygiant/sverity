// sverity-args: --top top
// Bit 1 of `{2{clk}}` is clk; bit 0 of `~{~(clk & en), ~clk}` is clk through two inversions, whatever gates its other bit.
// expect-none: LNT-047
module top(input logic clk, en, d, output logic q, r);
  logic [1:0] rep, mix;
  assign rep = {2{clk}};
  assign mix = ~{~(clk & en), ~clk};
  always_ff @(posedge rep[1]) q <= d;
  always_ff @(posedge mix[0]) r <= d;
endmodule
