// Adapted from pulp-platform/common_cells src/cc_clk_int_div.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2022 ETH Zurich, University of Bologna Copyright and related
// rights are licensed under the Solderpad Hardware License, Version 0.51 (the
// "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic ref_clk,
  input  logic rst_ni,
  output logic divided_clk,
  output logic slow_state
);
  logic divider_q;

  always_ff @(posedge ref_clk or negedge rst_ni) begin
    if (!rst_ni)
      divider_q <= 1'b0;
    else
      divider_q <= ~divider_q;
  end

  assign divided_clk = divider_q;

  always_ff @(posedge divided_clk or negedge rst_ni) begin
    if (!rst_ni)
      slow_state <= 1'b0;
    else
      slow_state <= ~slow_state;
  end
endmodule
