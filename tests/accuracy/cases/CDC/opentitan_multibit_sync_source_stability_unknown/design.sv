// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_multibit_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_i, input logic rst_ni, input logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic [7:0] sync_first_q, sync_second_q, check_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin sync_first_q <= '0; sync_second_q <= '0; check_q <= '0; data_o <= '0; end
    else begin
      sync_first_q <= data_i;
      sync_second_q <= sync_first_q;
      check_q <= sync_second_q;
      if (sync_second_q == check_q) data_o <= sync_second_q;
    end
endmodule
