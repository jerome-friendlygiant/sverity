// Adapted from opentitan hw/ip/i3c/rtl/i3c_target_rst.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk,
    input  logic ext_rst_ni,
    input  logic sw_rst_ni,
    input  logic enable_i,
    output logic local_reset_no,
    output logic active_o
);
    logic ext_sync_1_q, ext_sync_2_q;
    logic sw_sync_1_q, sw_sync_2_q;
    logic core_rst_ni;

    always_ff @(posedge clk or negedge ext_rst_ni) begin
        if (!ext_rst_ni) begin
            ext_sync_1_q <= 1'b0;
            ext_sync_2_q <= 1'b0;
        end else begin
            ext_sync_1_q <= 1'b1;
            ext_sync_2_q <= ext_sync_1_q;
        end
    end

    always_ff @(posedge clk or negedge sw_rst_ni) begin
        if (!sw_rst_ni) begin
            sw_sync_1_q <= 1'b0;
            sw_sync_2_q <= 1'b0;
        end else begin
            sw_sync_1_q <= 1'b1;
            sw_sync_2_q <= sw_sync_1_q;
        end
    end

    assign core_rst_ni = ext_sync_2_q & sw_sync_2_q;
    assign local_reset_no = ~core_rst_ni;

    always_ff @(posedge clk or negedge core_rst_ni) begin
        if (!core_rst_ni)
            active_o <= 1'b0;
        else
            active_o <= enable_i;
    end
endmodule
