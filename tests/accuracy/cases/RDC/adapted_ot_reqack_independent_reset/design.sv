// Adapted from opentitan hw/ip/prim/rtl/prim_sync_reqack.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk_src_i,
    input  logic clk_dst_i,
    input  logic rst_src_ni,
    input  logic rst_dst_ni,
    input  logic src_req_i,
    input  logic dst_accept_i,
    output logic src_ack_o,
    output logic dst_req_o
);
    logic src_toggle_q;
    logic dst_req_1_q, dst_req_2_q, dst_ack_q;
    logic src_ack_1_q, src_ack_2_q;

    always_ff @(posedge clk_src_i or negedge rst_src_ni) begin
        if (!rst_src_ni) begin
            src_toggle_q <= 1'b0;
            src_ack_1_q  <= 1'b0;
            src_ack_2_q  <= 1'b0;
        end else begin
            if (src_req_i && src_ack_o)
                src_toggle_q <= ~src_toggle_q;
            src_ack_1_q <= dst_ack_q;
            src_ack_2_q <= src_ack_1_q;
        end
    end

    always_ff @(posedge clk_dst_i or negedge rst_dst_ni) begin
        if (!rst_dst_ni) begin
            dst_req_1_q <= 1'b0;
            dst_req_2_q <= 1'b0;
            dst_ack_q   <= 1'b0;
        end else begin
            dst_req_1_q <= src_toggle_q;
            dst_req_2_q <= dst_req_1_q;
            if (dst_accept_i)
                dst_ack_q <= dst_req_2_q;
        end
    end

    assign src_ack_o = (src_toggle_q == src_ack_2_q);
    assign dst_req_o = (dst_req_2_q != dst_ack_q);
endmodule
