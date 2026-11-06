// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_timeout.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_chk_i,
  input  logic clk_ref_i,
  input  logic rst_chk_ni,
  input  logic rst_ref_ni,
  input  logic en_i,
  output logic timeout_o
);
  logic chk_toggle_q;
  logic sync1_q, sync2_q, seen_q;
  logic [2:0] count_q;

  always_ff @(posedge clk_chk_i or negedge rst_chk_ni) begin
    if (!rst_chk_ni) chk_toggle_q <= 1'b0;
    else            chk_toggle_q <= ~chk_toggle_q;
  end

  always_ff @(posedge clk_ref_i or negedge rst_ref_ni) begin
    if (!rst_ref_ni) begin
      sync1_q   <= 1'b0;
      sync2_q   <= 1'b0;
      seen_q    <= 1'b0;
      count_q   <= '0;
      timeout_o <= 1'b0;
    end else if (!en_i) begin
      sync1_q   <= chk_toggle_q;
      sync2_q   <= sync1_q;
      seen_q    <= sync2_q;
      count_q   <= '0;
      timeout_o <= 1'b0;
    end else begin
      sync1_q <= chk_toggle_q;
      sync2_q <= sync1_q;
      if (sync2_q != seen_q) begin
        seen_q    <= sync2_q;
        count_q   <= '0;
        timeout_o <= 1'b0;
      end else if (count_q != 3'd7) begin
        count_q   <= count_q + 3'd1;
        timeout_o <= (count_q >= 3'd6);
      end
    end
  end
endmodule
