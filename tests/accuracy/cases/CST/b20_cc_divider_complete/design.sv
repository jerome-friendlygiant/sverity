// Adapted from pulp-platform/common_cells src/cc_clk_int_div_static.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2023 ETH Zurich, University of Bologna. Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set. SPDX-License-Identifier: SHL-0.51

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       en_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o,
  output logic       clk_div_o
);
  logic [1:0] div_q;
  logic [7:0] data_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) div_q <= 2'b0;
    else        div_q <= div_q + 2'b01;
  end

  assign clk_div_o = div_q[1];

  always_ff @(posedge clk_div_o or negedge rst_ni) begin
    if (!rst_ni)      data_q <= '0;
    else if (en_i)    data_q <= data_i;
  end

  assign data_o = data_q;
endmodule
