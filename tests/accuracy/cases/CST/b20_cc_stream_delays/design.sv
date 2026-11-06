// Adapted from pulp-platform/common_cells src/cc_stream_register.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2022 ETH Zurich and University of Bologna. Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set. SPDX-License-Identifier: SHL-0.51

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       clr_i,
  input  logic       valid_i,
  output logic       ready_o,
  input  logic       ready_i,
  input  logic [15:0] data_i,
  output logic       valid_o,
  output logic [15:0] data_o
);
  logic [15:0] data_q;

  assign ready_o = ready_i | ~valid_o;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 1'b0;
      data_q  <= '0;
    end else if (clr_i) begin
      valid_o <= 1'b0;
      data_q  <= '0;
    end else if (ready_o) begin
      valid_o <= valid_i;
      if (valid_i) data_q <= data_i;
    end
  end

  assign data_o = data_q;
endmodule
