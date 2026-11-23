// C04
module c04(input logic clk_s, clk_d, input logic [7:0] data_s, output logic [7:0] data_d);
  always_ff @(posedge clk_d) data_d <= data_s;
endmodule
