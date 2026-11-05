// sverity-args: --top top
// expect-none: LNT-080
module top(input logic clk, input logic d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
