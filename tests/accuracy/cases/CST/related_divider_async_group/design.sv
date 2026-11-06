// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_clock_div.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  output logic div_clk,
  output logic result
);
  logic [1:0] count_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      count_q <= 2'b0;
    else
      count_q <= count_q + 2'b01;
  end

  assign div_clk = count_q[1];

  always_ff @(posedge div_clk or negedge rst_ni) begin
    if (!rst_ni)
      result <= 1'b0;
    else
      result <= ~result;
  end
endmodule
