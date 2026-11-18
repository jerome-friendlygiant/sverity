// sverity-args: --top top
// A `buf` primitive is a plain copy: the clock is forwarded, as through an alias net.
// expect: LNT-027 INFO 7
// expect-none: LNT-027 WARNING
module top(input logic clk, d, output logic q, output wire clk_o);
  always_ff @(posedge clk) q <= d;
  buf b0(clk_o, clk);
endmodule
