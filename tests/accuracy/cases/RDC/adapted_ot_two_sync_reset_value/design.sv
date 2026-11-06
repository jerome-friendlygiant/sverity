// Adapted from opentitan hw/ip/prim_generic/rtl/prim_flop_2sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk,
    input  logic rst_ni,
    input  logic enable_i,
    output logic ready_o
);
    logic first_q;

    always_ff @(posedge clk or negedge rst_ni) begin
        if (!rst_ni) begin
            first_q <= 1'b0;
            ready_o <= 1'b0;
        end else begin
            first_q <= 1'b1;
            ready_o <= first_q & enable_i;
        end
    end
endmodule
