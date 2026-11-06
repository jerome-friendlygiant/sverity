// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_sync_slow_fast.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int Width = 16
) (
  input logic clk_slow_i, input logic clk_fast_i, input logic rst_fast_ni,
  input logic [Width-1:0] wdata_i, output logic [Width-1:0] rdata_o
);
  logic slow_toggle_q, slow_meta_q, slow_sync_q, slow_prev_q;
  logic [Width-1:0] wdata_q;
  always_ff @(negedge clk_slow_i) begin slow_toggle_q <= ~slow_toggle_q; wdata_q <= wdata_i; end
  always_ff @(posedge clk_fast_i or negedge rst_fast_ni)
    if (!rst_fast_ni) begin slow_meta_q <= 1'b0; slow_sync_q <= 1'b0; slow_prev_q <= 1'b0; rdata_o <= '0; end
    else begin
      slow_meta_q <= slow_toggle_q; slow_sync_q <= slow_meta_q; slow_prev_q <= slow_sync_q;
      if (slow_sync_q != slow_prev_q) rdata_o <= wdata_q;
    end
endmodule
