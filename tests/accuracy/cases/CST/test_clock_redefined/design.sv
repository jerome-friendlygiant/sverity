// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_gating_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic async_en_i,
  output logic gated_clk,
  output logic state_q
);
  logic en_q;

  always_ff @(posedge clk_i) begin
    en_q <= async_en_i;
  end

  assign gated_clk = clk_i & en_q;

  always_ff @(posedge gated_clk) begin
    state_q <= ~state_q;
  end
endmodule
