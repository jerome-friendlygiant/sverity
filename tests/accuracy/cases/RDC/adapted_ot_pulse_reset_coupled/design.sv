// Adapted from opentitan hw/ip/prim/rtl/prim_pulse_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk_src_i,
    input  logic clk_dst_i,
    input  logic rst_src_ni,
    input  logic rst_dst_ni,
    input  logic src_pulse_i,
    output logic dst_pulse_o
);
    logic src_level_q;
    logic sync_1_q, sync_2_q, dst_level_q;
    logic dst_effective_rst_ni;

    assign dst_effective_rst_ni = rst_src_ni & rst_dst_ni;

    always_ff @(posedge clk_src_i or negedge rst_src_ni) begin
        if (!rst_src_ni)
            src_level_q <= 1'b0;
        else if (src_pulse_i)
            src_level_q <= ~src_level_q;
    end

    always_ff @(posedge clk_dst_i or negedge dst_effective_rst_ni) begin
        if (!dst_effective_rst_ni) begin
            sync_1_q   <= 1'b0;
            sync_2_q   <= 1'b0;
            dst_level_q <= 1'b0;
        end else begin
            sync_1_q   <= src_level_q;
            sync_2_q   <= sync_1_q;
            dst_level_q <= sync_2_q;
        end
    end

    assign dst_pulse_o = sync_2_q ^ dst_level_q;
endmodule
