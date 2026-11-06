module dual_port_receiver (
  input logic core_clk_i,
  input logic io_clk_i,
  input logic rst_ni,
  input logic command_i,
  input logic [7:0] io_data_i,
  output logic [7:0] result_o
);
  logic command_q;
  logic [7:0] io_data_q;
  logic [7:0] result_q;

  always_ff @(posedge core_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      command_q <= 1'b0;
    else
      command_q <= command_i;
  end

  always_ff @(posedge io_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      io_data_q <= '0;
    else
      io_data_q <= io_data_i;
  end

  always_ff @(posedge core_clk_i or negedge rst_ni) begin
    if (!rst_ni)
      result_q <= '0;
    else if (command_q)
      result_q <= io_data_q + 8'h01;
  end
  assign result_o = result_q;
endmodule

module top (
  input logic core_clk_i,
  input logic io_clk_i,
  input logic rst_ni,
  input logic command_i,
  input logic [7:0] io_data_i,
  output logic [7:0] result_o
);
  dual_port_receiver u_receiver (.*);
endmodule
