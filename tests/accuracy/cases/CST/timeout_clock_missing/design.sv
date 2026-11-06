// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_timeout.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_chk_i,
  input  logic clk_i,
  input  logic en_i,
  output logic timeout_o
);
  logic ack_q;
  logic [3:0] count_q;

  always_ff @(posedge clk_chk_i) begin
    ack_q <= en_i;
  end

  always_ff @(posedge clk_i) begin
    if (!en_i || ack_q)
      count_q <= 4'b0;
    else if (count_q != 4'hf)
      count_q <= count_q + 4'b1;
  end

  assign timeout_o = &count_q;
endmodule
