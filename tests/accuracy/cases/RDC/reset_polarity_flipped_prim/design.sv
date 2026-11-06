// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_rst_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_i,
  input  logic value_i,
  output logic value_o
);
  logic [1:0] reset_sync_q;

  always_ff @(posedge clk_i or posedge rst_i) begin
    if (rst_i)
      reset_sync_q <= 2'b11;
    else
      reset_sync_q <= {reset_sync_q[0], 1'b0};
  end

  always_ff @(posedge clk_i or negedge reset_sync_q[1]) begin
    if (!reset_sync_q[1])
      value_o <= 1'b0;
    else
      value_o <= value_i;
  end
endmodule
