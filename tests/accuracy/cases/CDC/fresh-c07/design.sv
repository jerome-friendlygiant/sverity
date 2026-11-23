module top(input logic clk_a, clk_b, input logic [7:0] data_a, output logic [7:0] data_b);
  logic [7:0] mem [0:7];
  always_ff @(posedge clk_a) mem[0] <= data_a;
  always_ff @(posedge clk_b) data_b <= mem[0];
endmodule
