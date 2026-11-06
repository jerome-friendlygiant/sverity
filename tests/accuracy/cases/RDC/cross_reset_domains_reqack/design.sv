// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_sync_reqack.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_src,
  input  logic clk_dst,
  input  logic rst_src_ni,
  input  logic rst_dst_ni,
  input  logic req_i,
  output logic ack_o
);
  logic req_src_q;
  logic [1:0] req_dst_sync_q;
  logic ack_dst_q;
  logic [1:0] ack_src_sync_q;

  always_ff @(posedge clk_src or negedge rst_src_ni) begin
    if (!rst_src_ni)
      req_src_q <= 1'b0;
    else if (req_i)
      req_src_q <= ~req_src_q;
  end

  always_ff @(posedge clk_dst or negedge rst_dst_ni) begin
    if (!rst_dst_ni)
      req_dst_sync_q <= 2'b00;
    else
      req_dst_sync_q <= {req_dst_sync_q[0], req_src_q};
  end

  always_ff @(posedge clk_dst or negedge rst_dst_ni) begin
    if (!rst_dst_ni)
      ack_dst_q <= 1'b0;
    else if (req_dst_sync_q[1] != ack_dst_q)
      ack_dst_q <= req_dst_sync_q[1];
  end

  always_ff @(posedge clk_src or negedge rst_src_ni) begin
    if (!rst_src_ni)
      ack_src_sync_q <= 2'b00;
    else
      ack_src_sync_q <= {ack_src_sync_q[0], ack_dst_q};
  end

  assign ack_o = ack_src_sync_q[1] == req_src_q;
endmodule
