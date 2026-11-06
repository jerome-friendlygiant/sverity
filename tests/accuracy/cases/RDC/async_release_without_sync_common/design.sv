// Adapted from pulp-platform/common_cells src/cc_rstgen_bypass.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic request_i,
  output logic request_q
);
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      request_q <= 1'b0;
    else
      request_q <= request_i;
  end
endmodule
