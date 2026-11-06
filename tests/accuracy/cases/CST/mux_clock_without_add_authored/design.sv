module clock_select (
  input logic clk_a_i,
  input logic clk_b_i,
  input logic select_b_i,
  output logic clk_o
);
  always_comb begin
    if (select_b_i)
      clk_o = clk_b_i;
    else
      clk_o = clk_a_i;
  end
endmodule

module selected_domain (
  input logic clk_i,
  input logic rst_ni,
  input logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic [7:0] data_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      data_q <= '0;
    else
      data_q <= data_i + 8'h3c;
  end
  assign data_o = data_q;
endmodule

module top (
  input logic clk_a_i,
  input logic clk_b_i,
  input logic select_b_i,
  input logic rst_ni,
  input logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic selected_clk;
  clock_select u_mux (.clk_a_i, .clk_b_i, .select_b_i, .clk_o(selected_clk));
  selected_domain u_domain (.clk_i(selected_clk), .rst_ni, .data_i, .data_o);
endmodule
