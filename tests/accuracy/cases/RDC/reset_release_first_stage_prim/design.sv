// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_flop_2sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic payload_i,
  output logic payload_o
);
  logic [1:0] release_q;
  logic release_ni;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      release_q <= 2'b00;
    else
      release_q <= {release_q[0], 1'b1};
  end

  assign release_ni = release_q[0];

  always_ff @(posedge clk_i or negedge release_ni) begin
    if (!release_ni)
      payload_o <= 1'b0;
    else
      payload_o <= payload_i;
  end
endmodule
