// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, d, output logic q);
  logic t;
  always_ff @(posedge clk) t = d;
  always_ff @(posedge clk) q <= t;
endmodule
