// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from hw/ip/prim/rtl/prim_count.sv. @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       incr_i,
  output logic [3:0] count_o
);
  logic [3:0] count_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      count_q = 4'd0;
    else if (incr_i)
      count_q = count_q + 4'd1;
  end
  assign count_o = count_q;
endmodule
