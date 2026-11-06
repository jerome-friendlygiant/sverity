// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_gating_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_i, input logic rst_ni, input logic test_en_i,
  input logic async_en_i, output logic en_o, output logic clk_o
);
  logic en_meta_q, en_sync_q, en_latched_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin en_meta_q <= 1'b0; en_sync_q <= 1'b0; end
    else begin en_meta_q <= async_en_i; en_sync_q <= en_meta_q; end
  always_ff @(negedge clk_i or negedge rst_ni)
    if (!rst_ni) en_latched_q <= 1'b0;
    else en_latched_q <= en_sync_q | test_en_i;
  assign en_o = en_sync_q;
  assign clk_o = clk_i & en_latched_q;
endmodule
