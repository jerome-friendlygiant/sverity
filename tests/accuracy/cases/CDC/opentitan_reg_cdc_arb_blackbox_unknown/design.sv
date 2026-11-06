// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_reg_cdc_arb.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

(* black_box *)
module cdc_arbiter_stub (
  input logic clk_src_i, input logic rst_src_ni,
  input logic clk_dst_i, input logic rst_dst_ni,
  input logic sw_req_i, input logic hw_update_i,
  input logic [7:0] hw_data_i,
  output logic sw_ack_o, output logic dst_req_o,
  output logic [7:0] sampled_data_o
);
endmodule

module top (
  input logic clk_src_i, input logic rst_src_ni,
  input logic clk_dst_i, input logic rst_dst_ni,
  input logic sw_req_i, input logic hw_update_i, input logic [7:0] hw_data_i,
  output logic sw_ack_o, output logic dst_req_o, output logic [7:0] sampled_data_o
);
  cdc_arbiter_stub u_arb (
    .clk_src_i, .rst_src_ni, .clk_dst_i, .rst_dst_ni,
    .sw_req_i, .hw_update_i, .hw_data_i,
    .sw_ack_o, .dst_req_o, .sampled_data_o
  );
endmodule
