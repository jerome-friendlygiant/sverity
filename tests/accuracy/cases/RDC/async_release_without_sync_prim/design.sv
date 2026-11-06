// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_rst_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_main,
  input  logic por_ni,
  input  logic enable_i,
  output logic enabled_o
);
  always_ff @(posedge clk_main or negedge por_ni) begin
    if (!por_ni)
      enabled_o <= 1'b0;
    else
      enabled_o <= enable_i;
  end
endmodule
