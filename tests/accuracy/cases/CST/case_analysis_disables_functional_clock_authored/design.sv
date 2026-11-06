module mode_clock_switch (
  input logic func_clk_i,
  input logic test_clk_i,
  input logic test_mode_i,
  output logic selected_clk_o
);
  always_comb begin
    if (test_mode_i)
      selected_clk_o = test_clk_i;
    else
      selected_clk_o = func_clk_i;
  end
endmodule

module mode_register (
  input logic clk_i,
  input logic reset_i,
  input logic [7:0] data_i,
  output logic [7:0] data_o
);
  always_ff @(posedge clk_i or posedge reset_i) begin
    if (reset_i)
      data_o <= '0;
    else
      data_o <= data_i;
  end
endmodule

module top (
  input logic func_clk_i,
  input logic test_clk_i,
  input logic test_mode_i,
  input logic reset_i,
  input logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic selected_clk;
  mode_clock_switch u_switch (.func_clk_i, .test_clk_i, .test_mode_i, .selected_clk_o(selected_clk));
  mode_register u_reg (.clk_i(selected_clk), .reset_i, .data_i, .data_o);
endmodule
