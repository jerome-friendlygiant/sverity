// sverity-args: --top top
// A latch that copies the clock while en is open holds its value otherwise: not a plain copy.
// expect: LNT-027 WARNING 8
// expect-none: LNT-027 INFO
module top(input logic clk, d, en, output logic q, o);
  logic l;
  always_ff @(posedge clk) q <= d;
  always_latch if (en) l = clk;
  assign o = l;
endmodule
