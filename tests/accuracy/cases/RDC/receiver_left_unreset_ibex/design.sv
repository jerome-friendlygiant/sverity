// Adapted from lowRISC/ibex rtl/ibex_fetch_fifo.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_src,
  input  logic clk_dst,
  input  logic rst_src_ni,
  input  logic rst_dst_ni,
  input  logic valid_i,
  input  logic [31:0] address_i,
  output logic [31:0] address_o
);
  logic valid_src_q;
  logic [1:0] valid_dst_q;

  always_ff @(posedge clk_src or negedge rst_src_ni) begin
    if (!rst_src_ni)
      valid_src_q <= 1'b0;
    else
      valid_src_q <= valid_i;
  end

  always_ff @(posedge clk_dst or negedge rst_dst_ni) begin
    if (!rst_dst_ni)
      valid_dst_q <= 2'b00;
    else
      valid_dst_q <= {valid_dst_q[0], valid_src_q};
  end

  always_ff @(posedge clk_dst) begin
    if (valid_dst_q[1])
      address_o <= address_i;
  end
endmodule
