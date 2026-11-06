// Adapted from pulp-platform/common_cells src/cc_edge_detect.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic d_i,
  output logic re_o,
  output logic fe_o
);
  logic [1:0] sync_q;
  logic       previous_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync_q    <= 2'b0;
      previous_q <= 1'b0;
    end else begin
      sync_q     <= {sync_q[0], d_i};
      previous_q <= sync_q[1];
    end
  end

  assign re_o = sync_q[1] & ~previous_q;
  assign fe_o = ~sync_q[1] & previous_q;
endmodule
