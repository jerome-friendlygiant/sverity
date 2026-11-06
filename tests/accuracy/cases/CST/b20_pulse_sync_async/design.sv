// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_pulse_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_src_i,
  input  logic clk_dst_i,
  input  logic rst_src_ni,
  input  logic rst_dst_ni,
  input  logic src_pulse_i,
  output logic dst_pulse_o
);
  logic src_level_q;
  logic dst_meta_q, dst_level_q;

  always_ff @(posedge clk_src_i or negedge rst_src_ni) begin
    if (!rst_src_ni) src_level_q <= 1'b0;
    else            src_level_q <= src_level_q ^ src_pulse_i;
  end

  always_ff @(posedge clk_dst_i or negedge rst_dst_ni) begin
    if (!rst_dst_ni) begin
      dst_meta_q  <= 1'b0;
      dst_level_q <= 1'b0;
    end else begin
      dst_meta_q  <= src_level_q;
      dst_level_q <= dst_meta_q;
    end
  end

  assign dst_pulse_o = dst_level_q ^ dst_meta_q;
endmodule
