// Adapted from pulp-platform/common_cells src/cc_rstgen_bypass.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic por_ni,
  input  logic isolate_i,
  input  logic data_i,
  output logic data_o
);
  logic local_rst_ni;
  assign local_rst_ni = por_ni & ~isolate_i;

  always_ff @(posedge clk_i or negedge local_rst_ni) begin
    if (!local_rst_ni)
      data_o <= 1'b0;
    else
      data_o <= data_i;
  end
endmodule
