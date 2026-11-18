// sverity-args: --top top
// The same clock also reaches a register's data input inside the module: that use keeps the WARNING beside the forwarding.
// expect: LNT-027 WARNING
module top(input logic clk, d, output logic q, output logic seen, output logic clk_o);
  always_ff @(posedge clk) q <= d;
  always_ff @(posedge clk) seen <= clk;
  assign clk_o = clk;
endmodule
