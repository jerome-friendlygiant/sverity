// sverity-args: --top top
// no always_ff block of the module resets, so none is inconsistent
// expect-none: LNT-068
module top(input logic clk, a, output logic q, r);
  always_ff @(posedge clk) q <= a;
  always_ff @(posedge clk) r <= q;
endmodule
