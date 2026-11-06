// Adapted from pulp-platform/common_cells src/cc_clk_mux_glitch_free.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2013-2022 ETH Zurich, University of Bologna. Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set. SPDX-License-Identifier: SHL-0.51

module top (
  input  logic       clk_a_i,
  input  logic       clk_b_i,
  input  logic       rst_ni,
  input  logic       sel_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o,
  output logic       clk_o
);
  assign clk_o = sel_i ? clk_b_i : clk_a_i;

  always_ff @(posedge clk_o or negedge rst_ni) begin
    if (!rst_ni) data_o <= '0;
    else        data_o <= data_i;
  end
endmodule
