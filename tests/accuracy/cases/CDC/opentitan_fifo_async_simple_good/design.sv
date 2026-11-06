// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_fifo_async_simple.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_wr_i, input logic rst_wr_ni, input logic wvalid_i,
  input logic [7:0] wdata_i, output logic wready_o,
  input logic clk_rd_i, input logic rst_rd_ni, output logic rvalid_o,
  input logic rready_i, output logic [7:0] rdata_o
);
  logic req_q, busy_q, req_meta_q, req_sync_q, rd_seen_q;
  logic ack_meta_q, ack_sync_q;
  logic [7:0] data_wr_q, data_rd_q;
  always_ff @(posedge clk_wr_i or negedge rst_wr_ni)
    if (!rst_wr_ni) begin req_q <= 1'b0; busy_q <= 1'b0; data_wr_q <= '0; ack_meta_q <= 1'b0; ack_sync_q <= 1'b0; end
    else begin
      ack_meta_q <= rd_seen_q; ack_sync_q <= ack_meta_q;
      if (wvalid_i && wready_o) begin req_q <= ~req_q; busy_q <= 1'b1; data_wr_q <= wdata_i; end
      else if (busy_q && ack_sync_q == req_q) busy_q <= 1'b0;
    end
  always_ff @(posedge clk_rd_i or negedge rst_rd_ni)
    if (!rst_rd_ni) begin req_meta_q <= 1'b0; req_sync_q <= 1'b0; rd_seen_q <= 1'b0; data_rd_q <= '0; end
    else begin
      req_meta_q <= req_q; req_sync_q <= req_meta_q;
      if (rvalid_o && rready_i) rd_seen_q <= req_sync_q;
      if (rvalid_o && !rready_i) data_rd_q <= data_wr_q;
      else if (rvalid_o) data_rd_q <= data_wr_q;
    end
  assign wready_o = ~busy_q;
  assign rvalid_o = req_sync_q != rd_seen_q;
  assign rdata_o = data_rd_q;
endmodule
