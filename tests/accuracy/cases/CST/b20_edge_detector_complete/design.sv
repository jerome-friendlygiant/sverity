// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_edge_detector.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic d_i,
  output logic q_sync_o,
  output logic q_posedge_pulse_o,
  output logic q_negedge_pulse_o
);
  logic sync_meta_q, sync_q;
  logic q_prev_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync_meta_q <= 1'b0;
      sync_q      <= 1'b0;
      q_prev_q    <= 1'b0;
    end else begin
      sync_meta_q <= d_i;
      sync_q      <= sync_meta_q;
      q_prev_q    <= sync_q;
    end
  end

  assign q_sync_o          = sync_q;
  assign q_posedge_pulse_o = sync_q & ~q_prev_q;
  assign q_negedge_pulse_o = ~sync_q & q_prev_q;
endmodule
