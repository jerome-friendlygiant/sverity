// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_edge_detector.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_b_ni,
  input  logic level_a,
  output logic event_b
);
  logic stage0_b;
  logic stage1_b;
  logic prior_b;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      stage0_b <= 1'b0;
      stage1_b <= 1'b0;
      prior_b <= 1'b0;
    end else begin
      stage0_b <= level_a;
      stage1_b <= stage0_b;
      prior_b <= stage1_b;
    end
  end

  assign event_b = stage0_b & ~prior_b;
endmodule
