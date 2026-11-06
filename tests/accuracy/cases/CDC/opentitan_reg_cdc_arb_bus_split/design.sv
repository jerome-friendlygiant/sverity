// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_reg_cdc_arb.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_b_ni,
  input  logic       update_a,
  input  logic [3:0] data_a,
  output logic [3:0] data_b
);
  logic [3:0] bit0_s0;
  logic [3:0] bit0_s1;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      bit0_s0 <= 4'h0;
      bit0_s1 <= 4'h0;
    end else begin
      bit0_s0[0] <= data_a[0];
      bit0_s1[0] <= bit0_s0[0];
      bit0_s0[1] <= data_a[1];
      bit0_s1[1] <= bit0_s0[1];
      bit0_s0[2] <= data_a[2];
      bit0_s1[2] <= bit0_s0[2];
      bit0_s0[3] <= data_a[3];
      bit0_s1[3] <= bit0_s0[3];
    end
  end

  assign data_b = bit0_s1;
endmodule
