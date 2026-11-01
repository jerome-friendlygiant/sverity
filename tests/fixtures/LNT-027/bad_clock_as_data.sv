// sverity-args: --top top
// expect: LNT-027 WARNING
module top(input logic clk, d, output logic q, output logic seen);
  always_ff @(posedge clk) q <= d;
  always_ff @(posedge clk) seen <= clk;
endmodule
