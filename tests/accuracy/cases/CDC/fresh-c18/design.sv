module top(input logic clk_a, clk_b, input logic bit_a, output logic bit_b);
  logic launch_a;
  always_ff @(posedge clk_a) launch_a <= bit_a;
  always_ff @(posedge clk_b) bit_b <= launch_a;
endmodule
