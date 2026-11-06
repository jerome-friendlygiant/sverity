// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_lc_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_src,
  input  logic clk_dst,
  input  logic clear_src_i,
  input  logic value_i,
  output logic value_o
);
  logic clear_src_q;
  logic dst_rst_ni;

  always_ff @(posedge clk_src) begin
    if (clear_src_i)
      clear_src_q <= 1'b1;
    else
      clear_src_q <= 1'b0;
  end

  assign dst_rst_ni = ~clear_src_q;

  always_ff @(posedge clk_dst or negedge dst_rst_ni) begin
    if (!dst_rst_ni)
      value_o <= 1'b0;
    else
      value_o <= value_i;
  end
endmodule
