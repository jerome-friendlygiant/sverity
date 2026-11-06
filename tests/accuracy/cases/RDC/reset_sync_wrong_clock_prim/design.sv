// Adapted from lowRISC/opentitan hw/ip/prim_generic/rtl/prim_flop_2sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Copyright lowRISC contributors (OpenTitan project). Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_main,
  input  logic clk_aux,
  input  logic por_ni,
  input  logic data_i,
  output logic data_o
);
  logic [1:0] release_aux_q;
  logic release_main_ni;

  always_ff @(posedge clk_aux or negedge por_ni) begin
    if (!por_ni)
      release_aux_q <= 2'b00;
    else
      release_aux_q <= {release_aux_q[0], 1'b1};
  end

  assign release_main_ni = release_aux_q[1];

  always_ff @(posedge clk_main or negedge release_main_ni) begin
    if (!release_main_ni)
      data_o <= 1'b0;
    else
      data_o <= data_i;
  end
endmodule
