// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_sync_reqack.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_src,
  input  logic clk_dst,
  input  logic rst_src_ni,
  input  logic request_i,
  output logic request_o
);
  logic src_release_q;
  logic dst_reset_ni;

  always_ff @(posedge clk_src or negedge rst_src_ni) begin
    if (!rst_src_ni)
      src_release_q <= 1'b0;
    else
      src_release_q <= 1'b1;
  end

  assign dst_reset_ni = src_release_q;

  always_ff @(posedge clk_dst or negedge dst_reset_ni) begin
    if (!dst_reset_ni)
      request_o <= 1'b0;
    else
      request_o <= request_i;
  end
endmodule
