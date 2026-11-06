// Adapted from pulp-platform/common_cells src/cc_sync_wedge.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_core,
  input  logic clk_ref,
  input  logic rst_ni,
  input  logic command_i,
  output logic command_q
);
  logic [1:0] release_ref_q;
  logic release_core_ni;

  always_ff @(posedge clk_ref or negedge rst_ni) begin
    if (!rst_ni)
      release_ref_q <= 2'b00;
    else
      release_ref_q <= {release_ref_q[0], 1'b1};
  end

  assign release_core_ni = release_ref_q[1];

  always_ff @(posedge clk_core or negedge release_core_ni) begin
    if (!release_core_ni)
      command_q <= 1'b0;
    else
      command_q <= command_i;
  end
endmodule
