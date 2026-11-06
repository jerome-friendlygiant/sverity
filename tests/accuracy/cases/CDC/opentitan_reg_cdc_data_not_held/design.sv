// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_reg_cdc.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_a_ni,
  input  logic       rst_b_ni,
  input  logic       update_a,
  input  logic [7:0] value_a,
  output logic [7:0] value_b,
  output logic       update_ack_b
);
  logic req_a;
  logic req_s0_b;
  logic req_s1_b;
  logic req_seen_b;

  always_ff @(posedge clk_a or negedge rst_a_ni) begin
    if (!rst_a_ni)
      req_a <= 1'b0;
    else if (update_a)
      req_a <= ~req_a;
  end

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      req_s0_b <= 1'b0;
      req_s1_b <= 1'b0;
      req_seen_b <= 1'b0;
      value_b <= 8'h00;
      update_ack_b <= 1'b0;
    end else begin
      req_s0_b <= req_a;
      req_s1_b <= req_s0_b;
      if (req_s1_b != req_seen_b) begin
        value_b <= value_a;
        req_seen_b <= req_s1_b;
        update_ack_b <= req_s1_b;
      end
    end
  end
endmodule
