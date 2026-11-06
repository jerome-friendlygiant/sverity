// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim_generic/rtl/prim_flop_2sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_b_ni,
  input  logic flag_a,
  output logic flag_b
);
  logic stage_b;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      stage_b <= 1'b0;
    end else begin
      stage_b <= flag_a;
    end
  end

  assign flag_b = stage_b;
endmodule
