module top(input logic clk_a, clk_b, input logic [3:0] data_a, output logic [3:0] data_b);
  logic [3:0] held_a;
  always_ff @(posedge clk_a) held_a <= data_a;
  always_ff @(posedge clk_b) data_b <= held_a;
endmodule
