// sverity-args: --top top
// A clock that only feeds clock-gating logic is not data.
// expect-none: LNT-027
module top(input logic clk, en, d, output logic q);
  logic g;
  assign g = clk & en;
  always_ff @(posedge g) q <= d;
endmodule
