// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_clock_meas.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       clk_ref_i,
  input  logic       rst_ni,
  input  logic       rst_ref_ni,
  input  logic       en_i,
  output logic       valid_o,
  output logic       fast_o,
  output logic       slow_o,
  output logic       ref_timeout_o
);
  logic [3:0] input_count_q;
  logic [3:0] ref_count_q;
  logic       en_ref_q;

  always_ff @(posedge clk_ref_i or negedge rst_ref_ni) begin
    if (!rst_ref_ni) begin
      en_ref_q       <= 1'b0;
      ref_count_q    <= '0;
      ref_timeout_o  <= 1'b0;
    end else begin
      en_ref_q      <= en_i;
      if (!en_i) ref_count_q <= '0;
      else if (ref_count_q != 4'hf) ref_count_q <= ref_count_q + 4'd1;
      ref_timeout_o <= (ref_count_q == 4'hf);
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      input_count_q <= '0;
      valid_o       <= 1'b0;
      fast_o        <= 1'b0;
      slow_o        <= 1'b0;
    end else if (!en_i) begin
      input_count_q <= '0;
      valid_o       <= 1'b0;
      fast_o        <= 1'b0;
      slow_o        <= 1'b0;
    end else begin
      if (input_count_q != 4'hf) input_count_q <= input_count_q + 4'd1;
      valid_o <= 1'b1;
      fast_o  <= input_count_q > 4'd10;
      slow_o  <= input_count_q < 4'd2;
    end
  end
endmodule
