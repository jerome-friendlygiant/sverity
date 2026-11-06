// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_fifo_async.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_wr,
  input  logic clk_rd,
  input  logic rst_wr_ni,
  input  logic rst_rd_ni,
  input  logic rst_data_ni,
  input  logic push_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic [1:0] wr_ptr_q;
  logic [1:0] wr_ptr_sync_q;
  logic [1:0] rd_ptr_q;

  always_ff @(posedge clk_wr or negedge rst_wr_ni) begin
    if (!rst_wr_ni)
      wr_ptr_q <= 2'b00;
    else if (push_i)
      wr_ptr_q <= wr_ptr_q + 2'b01;
  end

  always_ff @(posedge clk_rd or negedge rst_rd_ni) begin
    if (!rst_rd_ni)
      wr_ptr_sync_q <= 2'b00;
    else
      wr_ptr_sync_q <= wr_ptr_q;
  end

  always_ff @(posedge clk_rd or negedge rst_data_ni) begin
    if (!rst_data_ni)
      data_o <= 8'h00;
    else if (wr_ptr_sync_q != rd_ptr_q)
      data_o <= data_i;
  end

  always_ff @(posedge clk_rd or negedge rst_rd_ni) begin
    if (!rst_rd_ni)
      rd_ptr_q <= 2'b00;
    else if (wr_ptr_sync_q != rd_ptr_q)
      rd_ptr_q <= rd_ptr_q + 2'b01;
  end
endmodule
