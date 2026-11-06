// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_sync_reqack_data.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_src_i, input logic rst_src_ni, input logic src_req_i,
  input logic clk_dst_i, input logic rst_dst_ni, input logic dst_ack_i,
  input logic [15:0] data_i, output logic src_ack_o, output logic dst_req_o,
  output logic [15:0] data_o
);
  logic req_q, req_meta_q, req_sync_q, dst_seen_q;
  logic ack_meta_q, ack_sync_q;
  always_ff @(posedge clk_src_i or negedge rst_src_ni)
    if (!rst_src_ni) begin req_q <= 1'b0; ack_meta_q <= 1'b0; ack_sync_q <= 1'b0; end
    else begin req_q <= src_req_i ? ~req_q : req_q; ack_meta_q <= dst_seen_q; ack_sync_q <= ack_meta_q; end
  always_ff @(posedge clk_dst_i or negedge rst_dst_ni)
    if (!rst_dst_ni) begin req_meta_q <= 1'b0; req_sync_q <= 1'b0; dst_seen_q <= 1'b0; data_o <= '0; end
    else begin
      req_meta_q <= req_q; req_sync_q <= req_meta_q;
      if (dst_req_o && dst_ack_i) begin dst_seen_q <= req_sync_q; data_o <= data_i; end
    end
  assign dst_req_o = req_sync_q != dst_seen_q;
  assign src_ack_o = ack_sync_q == req_q;
endmodule
