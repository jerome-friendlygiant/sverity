// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_multibit_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_b_ni,
  input  logic [3:0] bus_a,
  output logic [3:0] bus_b
);
  logic bit0_s0, bit0_s1;
  logic bit1_s0, bit1_s1;
  logic bit2_s0, bit2_s1;
  logic bit3_s0, bit3_s1;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      {bit3_s0, bit2_s0, bit1_s0, bit0_s0} <= '0;
      {bit3_s1, bit2_s1, bit1_s1, bit0_s1} <= '0;
    end else begin
      bit0_s0 <= bus_a[0];
      bit0_s1 <= bit0_s0;
      bit1_s0 <= bus_a[1];
      bit1_s1 <= bit1_s0;
      bit2_s0 <= bus_a[2];
      bit2_s1 <= bit2_s0;
      bit3_s0 <= bus_a[3];
      bit3_s1 <= bit3_s0;
    end
  end

  assign bus_b = {bit3_s1, bit2_s1, bit1_s1, bit0_s1};
endmodule
