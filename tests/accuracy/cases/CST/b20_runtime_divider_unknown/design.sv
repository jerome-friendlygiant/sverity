// Adapted from pulp-platform/common_cells src/cc_clk_int_div.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2023 ETH Zurich, University of Bologna. Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set. SPDX-License-Identifier: SHL-0.51

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic [2:0] div_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o,
  output logic       clk_div_o
);
  logic [2:0] count_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      count_q   <= '0;
      clk_div_o <= 1'b0;
    end else if ((div_i == 3'd0) || (count_q == div_i - 3'd1)) begin
      count_q   <= '0;
      clk_div_o <= ~clk_div_o;
    end else begin
      count_q <= count_q + 3'd1;
    end
  end

  always_ff @(posedge clk_div_o or negedge rst_ni) begin
    if (!rst_ni) data_o <= '0;
    else        data_o <= data_i;
  end
endmodule
