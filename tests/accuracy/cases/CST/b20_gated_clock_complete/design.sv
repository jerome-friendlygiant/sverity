// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_gating_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       test_en_i,
  input  logic       async_en_i,
  input  logic [7:0] data_i,
  output logic       en_o,
  output logic [7:0] data_o,
  output logic       clk_o
);
  logic en_meta_q;
  logic [7:0] data_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      en_meta_q <= 1'b0;
      en_o      <= 1'b0;
    end else begin
      en_meta_q <= async_en_i;
      en_o      <= en_meta_q;
    end
  end

  assign clk_o = clk_i & (en_o | test_en_i);

  always_ff @(posedge clk_o or negedge rst_ni) begin
    if (!rst_ni) data_q <= '0;
    else        data_q <= data_i;
  end

  assign data_o = data_q;
endmodule
