// Adapted from pulp-platform/common_cells src/cc_rstgen.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic data_i,
  output logic data_o
);
  logic [1:0] release_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      release_q <= 2'b00;
    else
      release_q <= {release_q[0], 1'b1};
  end

  always_ff @(posedge clk_i or posedge release_q[1]) begin
    if (release_q[1])
      data_o <= 1'b0;
    else
      data_o <= data_i;
  end
endmodule
