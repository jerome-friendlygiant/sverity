// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim_generic/rtl/prim_clock_mux2.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk0_a,
  input  logic clk1_b,
  input  logic sel_ctrl,
  output logic clk_out
);
  assign clk_out = (sel_ctrl & clk1_b) | (~sel_ctrl & clk0_a);
endmodule
