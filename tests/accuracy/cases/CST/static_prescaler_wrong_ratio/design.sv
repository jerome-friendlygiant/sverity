// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_clock_div.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  output logic slow_clk,
  output logic state_q
);
  logic [2:0] count_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      count_q <= 3'b0;
    else
      count_q <= count_q + 3'b001;
  end

  assign slow_clk = count_q[2];

  always_ff @(posedge slow_clk or negedge rst_ni) begin
    if (!rst_ni)
      state_q <= 1'b0;
    else
      state_q <= ~state_q;
  end
endmodule
