// Adapted from pulp-platform/common_cells src/cc_clk_int_div_static.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2023 ETH Zurich, University of Bologna Copyright and related
// rights are licensed under the Solderpad Hardware License, Version 0.51 (the
// "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic ref_clk,
  input  logic rst_ni,
  output logic divided_clk,
  output logic status
);
  logic [1:0] count_q;

  always_ff @(posedge ref_clk or negedge rst_ni) begin
    if (!rst_ni)
      count_q <= 2'b0;
    else
      count_q <= count_q + 2'b01;
  end

  assign divided_clk = count_q[1];

  always_ff @(posedge divided_clk or negedge rst_ni) begin
    if (!rst_ni)
      status <= 1'b0;
    else
      status <= ~status;
  end
endmodule
