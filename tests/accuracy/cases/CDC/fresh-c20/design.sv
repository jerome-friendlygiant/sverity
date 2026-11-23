module top(input logic clk_a, clk_b, input logic bit_a, output logic bit_b);
  logic launch_a, s1, s2;
  always_ff @(posedge clk_a) launch_a <= bit_a;
  always_ff @(posedge clk_b) begin s1 <= launch_a; s2 <= s1; end
  always_comb bit_b = s2;
endmodule
