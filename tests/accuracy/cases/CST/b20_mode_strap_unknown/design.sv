// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_gate_gen.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       mode_i,
  input  logic [7:0] data_a_i,
  input  logic [7:0] data_b_i,
  output logic [7:0] data_o
);
  logic [7:0] selected_data;

  assign selected_data = mode_i ? data_b_i : data_a_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) data_o <= '0;
    else        data_o <= selected_data;
  end
endmodule
