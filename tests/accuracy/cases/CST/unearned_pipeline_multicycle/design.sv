// Adapted from pulp-platform/common_cells src/cc_counter.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       en_i,
  input  logic [7:0] d_i,
  output logic [7:0] result_o
);
  logic [7:0] count_q;
  logic [7:0] next_count;

  assign next_count = en_i ? count_q + d_i : count_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      count_q  <= 8'b0;
      result_o <= 8'b0;
    end else begin
      count_q  <= next_count;
      result_o <= next_count;
    end
  end
endmodule
