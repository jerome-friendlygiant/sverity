// Adapted from alexforencich/verilog-axis rtl/axis_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2014-2018 Alex Forencich. Permission is hereby granted under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk,
  input  logic       rst,
  input  logic [31:0] s_axis_tdata,
  input  logic       s_axis_tvalid,
  output logic       s_axis_tready,
  output logic [31:0] m_axis_tdata,
  output logic       m_axis_tvalid,
  input  logic       m_axis_tready
);
  logic [31:0] data_q;
  logic valid_q;

  assign s_axis_tready = ~valid_q | m_axis_tready;
  always_ff @(posedge clk) begin
    if (rst) begin
      valid_q <= 1'b0;
      data_q  <= '0;
    end else if (s_axis_tready) begin
      valid_q <= s_axis_tvalid;
      if (s_axis_tvalid) data_q <= s_axis_tdata;
    end
  end

  assign m_axis_tvalid = valid_q;
  assign m_axis_tdata  = data_q;
endmodule
