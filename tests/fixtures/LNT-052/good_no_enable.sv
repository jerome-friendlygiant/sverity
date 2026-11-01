// sverity-args: --top top
// expect-none: LNT-052
module top(input logic clk, a, output logic q);
  always_ff @(posedge clk) q <= a;
endmodule
