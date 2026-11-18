// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module reg_slice #(
  parameter int W = 16
) (
  input  logic         clk_i,
  input  logic         rst_ni,
  input  logic [W-1:0] data_i,
  input  logic         valid_i,
  output logic         ready_o,
  output logic [W-1:0] data_o,
  output logic         valid_o,
  input  logic         ready_i
);
  logic [W-1:0] data_q;
  logic         valid_q;

  assign ready_o = ~valid_q | ready_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q <= 1'b0;
      data_q  <= '0;
    end else if (ready_o) begin
      valid_q <= valid_i;
      if (valid_i) data_q <= data_i;
    end
  end

  assign data_o  = data_q;
  assign valid_o = valid_q;
endmodule

module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic [15:0] s_data_i,
  input  logic        s_valid_i,
  output logic        s_ready_o,
  output logic [15:0] m_data_o,
  output logic        m_valid_o,
  input  logic        m_ready_i
);
  logic [15:0] mid_data;
  logic        mid_valid;
  logic        mid_ready;

  reg_slice #(.W(16)) u_front (
    .clk_i, .rst_ni,
    .data_i(s_data_i), .valid_i(s_valid_i), .ready_o(s_ready_o),
    .data_o(mid_data), .valid_o(mid_valid), .ready_i(mid_ready)
  );

  reg_slice #(.W(16)) u_back (
    .clk_i, .rst_ni,
    .data_i(mid_data), .valid_i(mid_valid), .ready_o(mid_ready),
    .dat_o(m_data_o), .valid_o(m_valid_o), .ready_i(m_ready_i)
  );
endmodule
