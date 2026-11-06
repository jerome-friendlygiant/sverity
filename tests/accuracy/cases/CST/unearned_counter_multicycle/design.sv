// Adapted from pulp-platform/common_cells src/cc_stream_delay.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       valid_i,
  input  logic [7:0] payload_i,
  output logic       valid_o,
  output logic [7:0] payload_o
);
  logic [7:0] payload_q;
  logic       valid_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      payload_q <= 8'b0;
      valid_q   <= 1'b0;
      payload_o <= 8'b0;
      valid_o   <= 1'b0;
    end else begin
      payload_q <= payload_i;
      valid_q   <= valid_i;
      payload_o <= payload_q;
      valid_o   <= valid_q;
    end
  end
endmodule
