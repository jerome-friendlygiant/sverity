// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_pulse_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_fast,
  input  logic clk_slow,
  input  logic rst_slow_ni,
  input  logic pulse_fast,
  output logic pulse_slow
);
  always_ff @(posedge clk_slow or negedge rst_slow_ni) begin
    if (!rst_slow_ni)
      pulse_slow <= 1'b0;
    else
      pulse_slow <= pulse_fast;
  end
endmodule
