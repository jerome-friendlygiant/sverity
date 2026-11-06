// Adapted from opentitan hw/ip/prim_generic/rtl/prim_rst_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic       clk,
    input  logic       rst_ni,
    input  logic [3:0] din,
    input  logic       din_valid,
    output logic [3:0] dout,
    output logic       dout_valid
);
    logic rst_sync_1, rst_sync_2;

    always_ff @(posedge clk or negedge rst_ni) begin
        if (!rst_ni) begin
            rst_sync_1 <= 1'b0;
            rst_sync_2 <= 1'b0;
        end else begin
            rst_sync_1 <= 1'b1;
            rst_sync_2 <= rst_sync_1;
        end
    end

    always_ff @(posedge clk or negedge rst_ni) begin
        if (!rst_ni) begin
            dout_valid <= 1'b0;
            dout       <= '0;
        end else if (rst_sync_2) begin
            dout_valid <= din_valid;
            if (din_valid)
                dout <= din;
        end else begin
            dout_valid <= 1'b0;
        end
    end
endmodule
