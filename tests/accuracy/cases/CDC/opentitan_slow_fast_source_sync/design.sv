// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_sync_slow_fast.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_slow,
  input  logic       clk_fast,
  input  logic       rst_ni,
  input  logic [7:0] data_slow,
  output logic [7:0] data_fast
);
  logic [7:0] source_stage0;
  logic [7:0] source_stage1;

  always_ff @(posedge clk_slow or negedge rst_ni) begin
    if (!rst_ni) begin
      source_stage0 <= 8'h00;
      source_stage1 <= 8'h00;
    end else begin
      source_stage0 <= data_slow;
      source_stage1 <= source_stage0;
    end
  end

  assign data_fast = source_stage1;
endmodule
