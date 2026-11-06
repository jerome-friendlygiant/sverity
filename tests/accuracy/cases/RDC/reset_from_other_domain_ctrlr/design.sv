// Adapted from pulp-platform/common_cells src/cc_cdc_reset_ctrlr.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2021 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_a_ni,
  input  logic clear_a_i,
  input  logic state_i,
  output logic state_o
);
  logic clear_a_q;
  logic rst_b_ni;

  always_ff @(posedge clk_a or negedge rst_a_ni) begin
    if (!rst_a_ni)
      clear_a_q <= 1'b0;
    else if (clear_a_i)
      clear_a_q <= 1'b0;
    else
      clear_a_q <= 1'b1;
  end

  assign rst_b_ni = clear_a_q;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni)
      state_o <= 1'b0;
    else
      state_o <= state_i;
  end
endmodule
