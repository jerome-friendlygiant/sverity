// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_clock_mux2.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk0_i,
  input  logic clk1_i,
  output logic mux_clk,
  output logic state_q
);
  localparam logic SELECT_ONE = 1'b1;

  assign mux_clk = (SELECT_ONE & clk1_i) | (~SELECT_ONE & clk0_i);

  always_ff @(posedge mux_clk) begin
    state_q <= ~state_q;
  end
endmodule
